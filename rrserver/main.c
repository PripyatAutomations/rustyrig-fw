//
// rrserver/main.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <rrserver/link.h>
#include <rrserver/mesh.enroll.h>
#include <librrprotocol/crypto.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <glib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/faults.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <rrserver/help.h>
#include <rrserver/ptt.h>
#include <rrserver/thermal.h>
#include <rrserver/timer.h>
#include <rrserver/database.h>
#include <rrserver/files.h>
#include <rrserver/backend.h>
#include <rrserver/cfg.rig.h>
#include <librrprotocol/bridge.config.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/serial.h>
#include <rrserver/gps.h>
#include <librustyaxe/cfg.modules.h>
#include <rrserver/gpio.h>
#include <rrserver/cfg.radio.h>
#include <rrserver/si5351.h>
#include <rrserver/network.h>
#include <rrserver/amp.h>
#include <rrserver/atu.h>
#include <rrserver/filters.h>
#include <rrserver/protection.h>
#include <rrserver/mqtt.h>
#include <rrserver/au.h>
#include <rrserver/acl.h>
#include <rrserver/media.direct.h>
#include <rrserver/relay.h>
#include <rrserver/maintenance.h>

extern void rrserver_register_events(void);  // events.c
extern void rrserver_media_register_events(void);  // media.c
extern bool rrserver_media_init(void); // media.c
extern bool rrserver_media_audio_init(void); // media.c
extern void webcam_init(void);  // webcam.c
extern void webcam_shutdown(void);  // webcam.c
extern void audit_init(void);   // audit.c
extern void hostlog_init(void); // hostlog.c
#ifdef  USE_MONGOOSE
struct mg_mgr mg_mgr;
#endif

struct GlobalState rig;         // Global state
struct timespec mono_now;
struct timespec last_vfo_announce;
bool dying = 0;                 // Are we shutting down?
bool restarting = 0;            // Are we restarting?
time_t now = -1;                // time() at 1hz timer
time_t started = -1;            // time() when started
int auto_block_ptt = 0;         // Auto block PTT at boot?
char *rig_name = NULL;

// How often we will poll the backend in ms (default:1000)
int cfg_backend_poll_interval = 1000;
int cfg_backend_announce_interval = 10000;
int cfg_tick_interval = 100;

/* Refresh server settings which are consumed from globals by timers and protection code. Config reload replaces the dictionary atomically; this callback keeps
 * those long-lived mirrors in step with it. */
static bool rrserver_config_refresh(const char *key) {
   cfg_backend_poll_interval = cfg_get_int("backend.poll-interval", 1000);

   if (cfg_backend_poll_interval < 1) {
      cfg_backend_poll_interval = 1000;
   }
   cfg_backend_announce_interval = cfg_get_int("backend.announce-interval", 10000);

   if (cfg_backend_announce_interval < 1) {
      cfg_backend_announce_interval = 10000;
   }
   cfg_tick_interval = cfg_get_int("core.tick-interval", 100);

   if (cfg_tick_interval < 1) {
      cfg_tick_interval = 100;
   }
   protection_init();
   au_recording_config_refresh(NULL);
   Log(LOG_DEBUG, "config", "Refreshed cached server configuration");

   return true;
}

// These are used for restarting ourself using exec()
int my_argc = -1;
char **my_argv = NULL;

// Things that probably should be in headers... ;)
extern defconfig_t defcfg[];    // From defconfig.c
extern const char *configs[];
extern const int num_configs;
extern void timer_clock_tick_fn(void *arg);  // timer.clocktick.c
#ifdef USE_PROFILING
extern void timer_profile_dump_fn(void *arg);   // timer.clocktick.c
#endif

// Set minimum defaults, til we have EEPROM available
static uint32_t load_defaults(void) {
   rig.faultbeep = 1;
   rig.bc_standby = 1;
   rig.tr_delay = 50;

   return 0;
}

void shutdown_rig(uint32_t signum) {
   Log(LOG_CRIT, "core", "Shutting down by signal %d", signum);
   dying = 1;
   rr_ptt_set_all_off();
}

void restart_rig(void) {
   Log(LOG_CRIT, "core", "Restarting process...");
   host_cleanup();
   rrserver_files_fini();

   // Ensure argv is NULL-terminated (it should be, but defensively set it so...)
   my_argv[my_argc] = NULL;
   execv(my_argv[0], my_argv);

   // If execv fails
   Log(LOG_CRIT, "core", "restart_rig failed in execve(): %d: %s", errno, strerror(errno));
   exit(EXIT_FAILURE);
}

static void timer_check_faults_fn(void *arg) {
   if (check_faults()) {
      Log(LOG_CRIT, "core", "Fault detected, see log above. Rig halted!");
      // Kill TX and block PTT so clients can't re-key it.
      rr_ptt_set_all_off();
      rr_ptt_set_blocked(true);
   }
}

// This is called by our timer at 1000ms (1hz) rate by default
// cfg:backend.poll-interval is where this is set
static void timer_backend_poll_fn(void *arg) {
   /* One scheduler services every registered rig. A failed rig is recorded by the aggregate result but never prevents later registry entries from being polled.
    */
   rr_backend_poll_all();

   if (timespec_diff_ms(&mono_now, &last_vfo_announce) >= cfg_backend_announce_interval) {
      last_vfo_announce = mono_now;
   }
}

// We need to update this pretty often as it's used to time a lot of UI interactions
// See cfg:core.tick-interval
static void timer_tick_fn(void *arg) {
   clock_gettime(CLOCK_MONOTONIC, &mono_now);
}

static bool mesh_token_b64(const char *value, gsize expected) {
   if (!value || strlen(value) != ((expected + 2) / 3) * 4) return false;
   for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
      if (!(g_ascii_isalnum(*p) || *p == '+' || *p == '/' || *p == '=')) return false;
   gsize len = 0;
   guchar *raw = g_base64_decode(value, &len);
   char *canonical = raw ? g_base64_encode(raw, len) : NULL;
   bool ok = raw && len == expected && canonical && !strcmp(value, canonical);
   g_free(canonical);
   if (raw) { memset(raw, 0, len); g_free(raw); }
   return ok;
}

/* Validate every token field before using any issuer-provided network data. */
static bool mesh_invitation_valid(char *token) {
   char *fields[9] = {0};
   size_t n = 0;
   char *cursor = token;
   while (n < 9 && cursor && *cursor) {
      fields[n++] = cursor;
      char *end = strchr(cursor, '\n');
      if (!end) break;
      *end = 0;
      cursor = end + 1;
   }
   if (n != 8 || !cursor || *cursor ||
       strcmp(fields[0], "RUSTYRIG-JOIN-2") ||
       !g_uuid_string_is_valid(fields[1]) ||
       !g_uuid_string_is_valid(fields[3]) ||
       !fields[2][0] || strlen(fields[2]) > 253 ||
       !g_str_has_prefix(fields[4], "wss://") ||
       !g_str_has_suffix(fields[4], "/link") ||
       strlen(fields[4]) > 255 ||
       !mesh_token_b64(fields[5], 32) ||
       !mesh_token_b64(fields[6], 32))
      return false;
   for (const unsigned char *p = (const unsigned char *)fields[2]; *p; ++p)
      if (!(g_ascii_isalnum(*p) || *p == '.' || *p == '-' || *p == '_')) return false;
   for (const unsigned char *p = (const unsigned char *)fields[4]; *p; ++p)
      if (*p < 33 || *p > 126 || *p == '@' || *p == '#' || *p == '?') return false;
   /* Private key field is a PEM encoded as canonical base64. */
   size_t pem64_len = strlen(fields[7]);
   if (!pem64_len || pem64_len > 8192 || pem64_len % 4) return false;
   for (const unsigned char *p = (const unsigned char *)fields[7]; *p; ++p)
      if (!(g_ascii_isalnum(*p) || *p == '+' || *p == '/' || *p == '=')) return false;
   gsize pem_len = 0;
   guchar *pem = g_base64_decode(fields[7], &pem_len);
   char *canonical = pem ? g_base64_encode(pem, pem_len) : NULL;
   bool valid = pem && pem_len > 32 && pem_len < 6144 &&
      canonical && !strcmp(canonical, fields[7]) &&
      pem_len >= strlen("-----BEGIN PRIVATE KEY-----") &&
      !memcmp(pem, "-----BEGIN PRIVATE KEY-----", strlen("-----BEGIN PRIVATE KEY-----")) &&
      !memchr(pem, 0, pem_len);
   if (valid) {
      FILE *stream = fmemopen(pem, pem_len, "r");
      rr_crypto_key_t *key = stream ? rr_crypto_key_read_pem(stream) : NULL;
      if (stream) fclose(stream);
      uint8_t public_key[32];
      valid = key && rr_crypto_ed25519_public_bytes(key, public_key);
      rr_crypto_key_free(key);
      memset(public_key, 0, sizeof(public_key));
   }
   g_free(canonical);
   if (pem) { memset(pem, 0, pem_len); g_free(pem); }
   return valid;
}

/* Enrollment uses the same persistent server UUID as normal startup. */
static bool mesh_join_print_identity(void) {
#ifdef USE_SQLITE
   const char *db_path = cfg_get("path.db.master");
   if (!db_path || !*db_path) db_path = "/var/lib/rustyrig/db/master.db";
   sqlite3 *db = db_open(db_path);
   if (!db) {
      fprintf(stderr, "Mesh enrollment: unable to open master database at %s.\n", db_path);
      return false;
   }
   char *uuid = db_rig_uuid_get_or_create(db, "@server", "@node");
   sqlite3_close(db);
   if (!uuid || !g_uuid_string_is_valid(uuid)) {
      fprintf(stderr, "Mesh enrollment: unable to obtain persistent server UUID.\n");
      free(uuid);
      return false;
   }
   fprintf(stderr,
      "\nRustyRig mesh enrollment\n"
      "Your server UUID: %s\n\n"
      "Send this UUID to an administrator of the existing mesh.\n"
      "On an enrolled server, the administrator should run:\n"
      "  /link invite %s\n\n"
      "Ask them to send you the resulting private invitation token securely.\n"
      "Do not send them your server's private keys.\n\n", uuid, uuid);
   free(uuid);
   return true;
#else
   fprintf(stderr, "Mesh enrollment requires SQLite support.\n");
   return false;
#endif
}

/* The invitation is secret; never accept it in argv or print it. */
static int mesh_join_prepare(const char *path) {
   FILE *in = NULL;
   char *encoded = NULL;
   size_t len = 0, cap = 0;
   bool interactive = path == NULL;
   struct termios saved, hidden;
   bool terminal_restored_needed = false;
   int rc = EXIT_FAILURE;
   if (!mesh_join_print_identity()) return rc;
   if (interactive) {
      if (!isatty(STDIN_FILENO)) {
         fprintf(stderr, "Mesh enrollment requires an interactive terminal or token file.\n");
         return rc;
      }
      in = stdin;
      if (tcgetattr(STDIN_FILENO, &saved) == 0) {
         hidden = saved;
         hidden.c_lflag &= ~(ECHO | ICANON);
         hidden.c_cc[VMIN] = 1;
         hidden.c_cc[VTIME] = 0;
         terminal_restored_needed = tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) == 0;
      }
      if (!terminal_restored_needed) {
         fprintf(stderr, "Cannot disable terminal echo; refusing to read secret.\n");
         return rc;
      }
      fprintf(stderr, "Paste base64 mesh invitation token, then press Enter (visible on screen): ");
      fflush(stderr);
   } else {
      in = fopen(path, "rb");
      if (!in) {
         fprintf(stderr, "Cannot open invitation file: %s\n", strerror(errno));
         return rc;
      }
   }
   /* Bounded input; invite private key is small. No arbitrary-size reads. */
   cap = 16385;
   encoded = calloc(1, cap);
   if (!encoded) goto done;
   int ch;
   if (interactive) {
      /* Read beyond the canonical TTY line limit; echo explicitly so the operator
       * can see the paste without terminal-generated control characters. */
      while (true) {
         unsigned char byte;
         ssize_t n = read(STDIN_FILENO, &byte, 1);
         if (n < 0 && errno == EINTR) continue;
         if (n != 1) {
            fprintf(stderr, "\nUnable to read invitation from terminal.\n");
            goto done;
         }
         if (byte == '\r' || byte == '\n') break;
         if (byte < 32 || byte == 127) {
            if (byte != 127 && byte != 8) continue;
         }
         if (byte == 127 || byte == 8) {
            if (len) {
               --len;
               (void)write(STDERR_FILENO, "\b \b", 3);
            }
            continue;
         }
         if (len >= 16384) {
            fprintf(stderr, "\nInvitation exceeds maximum length.\n");
            goto done;
         }
         encoded[len++] = (char)byte;
         (void)write(STDERR_FILENO, &byte, 1);
      }
      fprintf(stderr, "\nReceived %zu invitation characters. Validating...\n", len);
   } else {
      while ((ch = fgetc(in)) != EOF) {
         if (len >= 16384) {
            fprintf(stderr, "\nInvitation exceeds maximum length.\n");
            goto done;
         }
         encoded[len++] = (char)ch;
      }
   }
   if (!len) {
      fprintf(stderr, "\nEmpty invitation.\n");
      goto done;
   }
   /* Accept line-wrapped base64 but reject arbitrary punctuation. */
   for (size_t i = 0; i < len; i++)
      if (!(g_ascii_isalnum(encoded[i]) || encoded[i] == '+' ||
            encoded[i] == '/' || encoded[i] == '=' ||
            encoded[i] == '\n' || encoded[i] == '\r')) {
         fprintf(stderr, "\nInvalid base64 invitation.\n");
         goto done;
      }
   gsize decoded_len = 0;
   guchar *raw = g_base64_decode(encoded, &decoded_len);
   if (!raw || decoded_len > 12000 || decoded_len < 20) {
      fprintf(stderr, "\nInvalid invitation encoding.\n");
      g_free(raw);
      goto done;
   }
   /* The decoded token must have an explicit terminator before parsing. */
   char *token = g_malloc(decoded_len + 1);
   memcpy(token, raw, decoded_len);
   token[decoded_len] = 0;
   bool valid = !memchr(raw, 0, decoded_len) && mesh_invitation_valid(token);
   if (!valid)
      fprintf(stderr, "\nMalformed or unsupported mesh invitation.\n");
   else {
      char *parts[8], *next = token;
      for (size_t i = 0; i < 8; ++i) {
         parts[i] = next;
         next += strlen(next) + 1;
      }
      rc = rr_mesh_enroll_run(parts) ? EXIT_SUCCESS : EXIT_FAILURE;
   }
   memset(token, 0, decoded_len + 1);
   g_free(token);
   memset(raw, 0, decoded_len);
   g_free(raw);
done:
   if (terminal_restored_needed) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
      fputc('\n', stderr);
   }
   if (encoded) {
      memset(encoded, 0, cap);
      free(encoded);
   }
   if (!interactive) fclose(in);
   return rc;
}

int main(int argc, char **argv) {
   // save for restarting later
   my_argc = argc;
   my_argv = argv;

   // Apply the hard-coded defaults from defconfig.c FIRST, so keys missing
   // from the user's config fall back to them.
   if (!default_cfg) {
      default_cfg = dict_new();
   }
   cfg_set_defaults(default_cfg, defcfg);
   cfg = default_cfg;

   // Initialize some early state
   now = started = time(NULL);

   // Register config section callbacks. Sections other than [general]/[server:*]
   // are dropped by cfg_load unless a callback claims them.
   if (!rr_radio_config_init() || !rr_si5351_config_init() || !rr_gpio_config_init() || !rrserver_files_config_init() || !rrserver_acl_config_init()
      || !rrserver_peer_config_init() || !rrserver_direct_config_init() || !rr_rig_config_init() || !rrp_bridge_config_init()) {
      Log(LOG_CRIT, "config", "Unable to register server configuration sections");

      return EXIT_FAILURE;
   }
   cfg_modules_init();

   if (!rr_serial_config_register()) {
      return EXIT_FAILURE;
   }
   cfg_add_callback(NULL, "fwdsp", config_fwdsp_section_cb);
   // [pipelines] keys land as pipeline:<codec>.<dir> -- the format bin/fwdsp
   // looks up with cfg_get() (see fwdsp/fwdsp.c)
   cfg_add_callback(NULL, "pipelines", config_pipeline_section_cb);

   event_init();
   rrserver_register_events();

   enum rr_maintenance_action maintenance = RR_MAINT_NONE;
   bool mesh_join = false;
   const char *mesh_join_path = NULL;
   static const struct option maintenance_options[] = {
      {
         "purge-recordings", no_argument, NULL, 1001
      },
      {
         "reset-db", no_argument, NULL, 1002
      },
      {
         "reset-rooms", no_argument, NULL, 1003
      },
      {
         "reset-logs", no_argument, NULL, 1004
      },
      {
         "reset-audit", no_argument, NULL, 1005
      },
      {
         "reset-adminpw", no_argument, NULL, 1006
      },
      {
         "reset-uuids", no_argument, NULL, 1007
      },
      {
         "help", no_argument, NULL, 'h'
      },
      {
         "mesh-join", optional_argument, NULL, 1008
      },
      {
         NULL, 0, NULL, 0
      }
   };

   /* getopt_long accepts unique prefixes by default; destructive commands deliberately require their entire long option spelling. */
   for (int i = 1 ; i < argc ; ++i) {
      if (strncmp(argv[i], "--", 2) || !strcmp(argv[i], "--")) {
         continue;
      }
      bool exact = false;

      for (size_t j = 0 ; maintenance_options[j].name ; ++j) {
         if (!strncmp(argv[i] + 2, maintenance_options[j].name, strlen(maintenance_options[j].name)) &&
              (argv[i][2 + strlen(maintenance_options[j].name)] == 0 ||
               (!strcmp(maintenance_options[j].name, "mesh-join") &&
                argv[i][2 + strlen(maintenance_options[j].name)] == '='))) {
            exact = true;
            break;
         }
      }

      if (!exact) {
         fprintf(stderr, "Unrecognized long option %s (full names only; no --force).\n", argv[i]);

         return EXIT_FAILURE;
      }
   }

   int opt;
   while ((opt = getopt_long(argc, argv, "f:hr:", maintenance_options, NULL)) != -1) {
      switch (opt) {
         case 1008:
            if (mesh_join || maintenance != RR_MAINT_NONE) {
               fprintf(stderr, "Specify only one one-shot operation.\n");
               return EXIT_FAILURE;
            }
            mesh_join = true;
            mesh_join_path = optarg;
            break;
         case 1001:
         case 1002:
         case 1003:
         case 1004:
         case 1005:
         case 1006:
         case 1007: {
            if (maintenance != RR_MAINT_NONE || mesh_join) {
               fprintf(stderr, "Specify only one maintenance operation per invocation.\n");

               return EXIT_FAILURE;
            }
            maintenance = (enum rr_maintenance_action)(opt - 1000);
            break;
         }
         case 'f': {
            config_file = strdup(optarg);
            break;
         }
         case 'r': {
            rig_name = strdup(optarg);
            break;
         }
         case 'h': {
            printf("Usage: %s [-f CONFIG] [-r RIGNAME] [MAINTENANCE OPTION]\n", argv[0]);
            puts("  -h, --help           Show this help");
            puts("  -f CONFIG           Use this server configuration file");
            puts("  -r RIGNAME          Select a rig configuration");
            puts("  --mesh-join [FILE]  Enroll permanent server identity via pinned HTTPS; prompts if omitted");
            puts("\nMaintenance (requires stopped server):");
            puts("  --purge-recordings  Remove known audio recordings");
            puts("  --reset-db          DELETE whole DB: users, credentials, rooms, grants, history");
            puts("  --reset-rooms       Clear rooms and VFO-room bindings; keep chat history");
            puts("  --reset-logs        Clear chat and PTT logs; keep audit and recordings");
            puts("  --reset-audit       Clear the audit log only");
            puts("  --reset-adminpw     Prompt for NEW admin password twice, hidden (8-256 chars)");
            puts("                      Update account directly; no random password or file");
            puts("  --reset-uuids       Regenerate room UUIDs and clear rig/VFO UUID mappings");
            puts("\nRequires an interactive terminal and YES <full-option> confirmation.");
            puts("Only one full maintenance option per invocation; no --force.");
            puts("Commands exit without starting rrserver. Back up data first.");
            puts("Uses configured path.db.master or fwdsp:recording.path.");

            return EXIT_SUCCESS;
         }
         default: {
            fprintf(stderr, "Unknown argument. Try %s --help\n", argv[0]);

            return EXIT_FAILURE;
         }
      }
   }
   if (mesh_join && optind < argc) {
      if (mesh_join_path || optind + 1 != argc) {
         fprintf(stderr, "Specify at most one invitation file.\n");
         return EXIT_FAILURE;
      }
      mesh_join_path = argv[optind];
   }
   // load config (posix hosts)
   char *fullpath = NULL;
   rig.log_level = LOG_CRAZY;

   if (config_file) {
      if (!(cfg = cfg_load(config_file))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
         free((void *)config_file);
         config_file = NULL;
         exit(1);
      }
   }

   if (!config_file && (fullpath = find_file_by_list(configs, num_configs))) {
      config_file = strdup(fullpath);

      if (!(cfg = cfg_load(fullpath))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      free(fullpath);
   }

   if (!config_file) {
      // Use default settings builtin
      fprintf(stderr, "No config found :(\n");
      exit(1);
   }

   if (mesh_join) return mesh_join_prepare(mesh_join_path);

   const char *module_dir = cfg_get("path.modules");
   struct stat module_stat;

   if (!module_dir || module_dir[0] != '/' || stat(module_dir, &module_stat) != 0 || !S_ISDIR(module_stat.st_mode)) {
      Log(LOG_CRIT, "modules", "Invalid installed module directory '%s'. Install rrserver modules and set path.modules to an existing absolute directory.",
         module_dir ? module_dir : "(unset)");

      return EXIT_FAILURE;
   }

   if (maintenance != RR_MAINT_NONE) {
      return rrserver_maintenance_run(maintenance);
   }

   if (!rrserver_maintenance_lock()) {
      return EXIT_FAILURE;
   }

   if (!rr_radio_config_prepare() || !rrserver_acl_validate()) {
      return EXIT_FAILURE;
   }
   printf("Loading config %s\n", config_file);
   // apply some global configuration
   char *logfile = cfg_get_path("log.file");
   logger_init((logfile ? logfile : "-"), false);

   if (logfile) {
      free(logfile);
      logfile = NULL;
   }

   char *ua_ban_path = cfg_get_path("net.http.ua-bans");

   if (ua_ban_path) {
      if (load_http_ua_bans(ua_ban_path)) {
         Log(LOG_WARN, "http", "Unable to load HTTP User-Agent bans from %s", ua_ban_path);
      }
      free(ua_ban_path);
   }

   srand((unsigned int)now);
   host_init();

   Log(LOG_INFO, "core", "rustyrig radio firmware v%s starting...", VERSION);
   memset(&rig, 0, sizeof(struct GlobalState));
   load_defaults();

// Core dump setup
#ifdef USE_COREDUMPS_SERVER
   struct rlimit rl = {
      .rlim_cur = RLIM_INFINITY,
      .rlim_max = RLIM_INFINITY
   };
   setrlimit(RLIMIT_CORE, &rl);
#else
   struct rlimit rl = {
      0, 0
   };
   setrlimit(RLIMIT_CORE, &rl);
#endif // USE_COREDUMPS_SERVER

#ifdef  USE_SQLITE
   char *masterdb_path = cfg_get_path("path.db.master");

   if (!masterdb_path) {
      masterdb_path = strdup(MASTERDB_PATH);
   }

   if (!(masterdb = db_open(masterdb_path))) {
      Log(LOG_CRIT, "core", "Cant open master db at %s", masterdb_path);
      free((void *)masterdb_path);
      exit(EXIT_FAILURE);
   }
   uint32_t default_vfo_mask = rr_rig_config_default_vfo_mask();
   ws_set_authoritative_vfo_mask(default_vfo_mask);
   free((void *)masterdb_path);
   audit_init();                // Store LOG_AUDIT level Log() messages in the db (audit.c)
#endif // USE_SQLITE
   hostlog_init();              // Stream Log() lines to FLAG_SYSLOG clients (hostlog.c)

   reload_event_add(NULL, rrserver_config_refresh, "refresh cached server settings after config reload");
   rrserver_config_refresh(NULL);
   timer_init();
   rig.hardware = rr_hardware_new();

   if (!rr_gpio_discover(rig.hardware) || !rr_gpio_configure(rig.hardware) || !rr_si5351_configure(rig.hardware) || !rr_radio_hardware_register(rig.hardware)) {
      Log(LOG_CRIT, "hardware", "Unable to initialize attached hardware mappings");
      rr_hardware_free(rig.hardware);
      rig.hardware = NULL;

      return EXIT_FAILURE;
   }
   Log(LOG_INFO, "hardware", "Registered %zu attached hardware resources", rr_hardware_count(rig.hardware));

#ifdef  USE_EEPROM

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }
#endif // USE_EEPROM

   /* EEPROM-backed settings may have augmented the config, so refresh the same cached values once more before timers and backends start. */
   rrserver_config_refresh(NULL);

//   i2c_init();
//   gui_init();

   // Print the serial #
   const char *s = cfg_get("device.serial");
   int serial_tmp = 0;

   if (s) {
      serial_tmp = atoi(s);
   }

#ifdef  USE_EEPROM

   if (!s || serial_tmp == 0) {
      rig.serial = get_serial_number();
   }
#endif
   Log(LOG_INFO, "core", "Device serial number: %lu", rig.serial);

   // apply some configuration from the eeprom
#ifdef  USE_EEPROM
   auto_block_ptt = eeprom_get_bool("features/auto-block-ptt");
#endif

   // Initialize add-in cards
   // XXX: This should be done by enumerating the bus eventually
   filter_init_all();
   rr_amp_init_all();
   rr_atu_init_all();

   if (auto_block_ptt) {
      Log(LOG_INFO, "core", "*** Enabling PTT block at startup - change features/auto-block-ptt to false to disable ***");
      rr_ptt_set_blocked(true);
   }

   if (rr_io_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error init i/o subsys ***");
      set_fault(FAULT_IO_ERROR);
      exit(EXIT_FAILURE);
   }

   if (!rrserver_bridges_prepare() || rr_backend_init()) {
      Log(LOG_CRIT, "core", "*** Failed init backend ***");
      set_fault(FAULT_BACKEND_ERR);
      exit(EXIT_FAILURE);
   }

   if (rrserver_rig_rooms_init()) {
      Log(LOG_CRIT, "core", "Unable to configure rig rooms");
      exit(EXIT_FAILURE);
   }

   // Provision the media channels (RX/TX audio per exposed VFO)
   if (rrserver_media_init()) {
      Log(LOG_CRIT, "core", "Unable to provision rig media");
      exit(EXIT_FAILURE);
   }


#ifdef  USE_CAT

   if (rr_cat_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error CAT ***");
      set_fault(FAULT_CAT_ERROR);
      exit(EXIT_FAILURE);
   }
#endif

//   rr_au_init();
//   dds_init();
   bool fwdsp_ready = !fwdsp_init();

   if (!fwdsp_ready) {
      Log(LOG_CRIT, "fwdsp", "fwdsp manager failed to initialize; audio will be unavailable");
   }

   // Launch the persistent callsign helper while the radio is coming online.
   // Readiness is checked when the first /qrz or /grid request arrives so a
   // slow helper cannot delay the websocket server from starting.
   if (!ws_callsign_lookup_init()) {
      Log(LOG_WARN, "callsign", "Callsign lookup helper was not ready at radio initialization; lookups will retry on demand");
   }

   // Network connectivity
   show_network_info();
#ifdef  USE_EEPROM
   show_pin_info();
#endif // USE_EEPROM

// Bring up libmongoose for the websocket/mqtt servers & mqtt client
#if     defined(USE_MONGOOSE)
#if     defined(HTTP_DEBUG_CRAZY)
   mg_log_set(MG_LL_DEBUG);
#else // HTTP_DEBUG_CRAZY
   mg_log_set(MG_LL_ERROR);
#endif // HTTP_DEBUG_CRAZY

   mg_mgr_init(&mg_mgr);

   if (!rrserver_links_init(&mg_mgr)) {
      Log(LOG_CRIT, "link", "Unable to initialize configured server links");
      mg_mgr_free(&mg_mgr);
      rrserver_links_fini();
      rrserver_relay_fini();

      return 1;
   }

   // Camera IPC must be attached after the manager is initialized.
   if (fwdsp_ready) {
      webcam_init();
   }

   if (fwdsp_ready && !rrserver_media_audio_init()) {
      Log(LOG_CRIT, "pcm.hub", "Rig audio hub initialization failed");
   }
#if     defined(USE_HTTP)
   http_init(&mg_mgr);
#endif // USE_HTTP
#if     defined(USE_MQTT)
   mqtt_server_init(&mg_mgr);
   mqtt_client_init();
#endif

   // Update the clock (now) once a second
   mg_timer_add(&mg_mgr, 1000, MG_TIMER_REPEAT, timer_clock_tick_fn, &mg_mgr);

// Refresh statistics every five minutes, including non-profiling builds.
// Handler/codec timing dumps remain guarded by USE_PROFILING.
   mg_timer_add(&mg_mgr, 300000, MG_TIMER_REPEAT, timer_profile_dump_fn, &mg_mgr);

   // Update the clock (now) once a second
   // Check for faults/protection every 150ms
   mg_timer_add(&mg_mgr, 150, MG_TIMER_REPEAT, timer_check_faults_fn, &mg_mgr);

   // rig polling
   mg_timer_add(&mg_mgr, cfg_backend_poll_interval, MG_TIMER_REPEAT, timer_backend_poll_fn, &mg_mgr);

   // 'precise' timekeeping
   mg_timer_add(&mg_mgr, cfg_tick_interval, MG_TIMER_REPEAT, timer_tick_fn, &mg_mgr);

#endif // USE_MONGOOSE

   Log(LOG_INFO, "core", "Radio initialization completed. Enjoy!");

   if (rrserver_gps_init()) {
      return EXIT_FAILURE;
   }
   bool modules_loaded = false;

   for (int i = 0 ;; i++) {
      const char *module = cfg_modules_get(i, NULL);

      if (!module) {
         break;
      }

      if (!rr_load_module(module)) {
         Log(LOG_CRIT, "modules", "Required server module '%s' could not be loaded from %s", module, module_dir);

         return EXIT_FAILURE;
      }

      modules_loaded = true;
   }

   rrserver_serial_init();
   (void)modules_loaded;

   if (!rrserver_bridges_start(config_file)) {
      Log(LOG_CRIT, "bridge", "Bridge configuration invalid");

      return EXIT_FAILURE;
   }

   // Main loop
   while (1) {
      extern void rrserver_objects_poll(void);
      rr_backend_drain_all();
      rrserver_bridges_poll();
      rrserver_relay_poll();
      rrserver_objects_poll();
      event_emit("server.poll", NULL, NULL);
      bool serial_active = rrserver_serial_poll();
      (void)serial_active;
#ifdef  USE_MONGOOSE
      // Reap any exited fwdsp children (flag set by the SIGCHLD handler)
      fwdsp_reap_children();

      // Process Mongoose HTTP and MQTT events, this should be at the end of
      // loop so all data is ready
      mg_mgr_poll(&mg_mgr, 10);
      rrserver_links_poll();
#endif

      if (dying) {
         break;
      }
   }
   extern void rrserver_objects_fini(void);

   // Unload configured modules while the event bus and transports still exist.
   for (int i = 0 ;; i++) {
      const char *module = cfg_modules_get(i, NULL);

      if (!module) {
         break;
      }

      // Backend code remains mapped until its workers and instances are gone.
      if (!strcmp(module, "rrserver-hamlib") || !strcmp(module, "rrserver-hamlib.so")) {
         continue;
      }

      if (rr_find_loaded_module(module)) {
         rr_unload_module(module);
      }
   }

   rrserver_bridges_stop();
   rrserver_serial_fini();
   rrserver_gps_fini();
   webcam_shutdown();
   rrserver_objects_fini();

   if (fwdsp_ready) {
      fwdsp_fini();
   }
   rr_backend_fini();
   rr_hardware_free(rig.hardware);
   rig.hardware = NULL;

   // Retry deferred module unloads after the backend callback owners are gone.
   for (int i = 0 ;; i++) {
      const char *module = cfg_modules_get(i, NULL);

      if (!module) {
         break;
      }

      if (rr_find_loaded_module(module)) {
         rr_unload_module(module);
      }
   }

   if (rr_find_loaded_module("rrserver-hamlib")) {
      rr_unload_module("rrserver-hamlib");
   }
   host_cleanup();

#ifdef  USE_MONGOOSE
   mg_mgr_free(&mg_mgr);
   rrserver_links_fini();
   rrserver_relay_fini();
#endif

   event_shutdown();
   cfg_fini();

   if (restarting) {
      restart_rig();
   }

   return 0;
}
