//
// rrclient/main.c: Core of the client
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <rrclient/database.h>
#include <getopt.h>
#include <stddef.h>
#include <rrclient/identity.store.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librustyaxe/termkey.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.binframe.h>
#include <librrprotocol/cfg.fwdsp.h>
#include <librrprotocol/rtp.h>
#include <librustyaxe/cfg.modules.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <glib.h>
extern defconfig_t defcfg[];
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#define MAX_WINDOWS 32
#define INPUT_HISTORY_MAX 64
#include <rrclient/ui.h>
#include <rrclient/media.h>
#include <rrclient/frontend.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.connect.h>
#include <rrclient/rooms.h>
#include <rrclient/dcc.h>
#include <rrclient/media.direct.h>
#include <rrclient/config.layers.h>
#include <rrclient/database.h>
#include <rrclient/cat.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/vfo.h>
#include <rrclient/ui.statusbar.h>

#ifdef USE_MONGOOSE
extern struct mg_mgr mgr;
#endif // defined(USE_MONGOOSE)

/* A local TX inhibit. A zero-percent GTK power setting cannot key RF, but release commands are always allowed through. */
int rrclient_tx_power_percent = 100;
bool rrclient_ptt_allowed(void) {
   return rrclient_tx_power_percent > 0;
}
void rrclient_send_ptt_command(rrconn_t *connection, const char *vfo, bool on, const char *room) {
   if (on && !rrclient_ptt_allowed()) {
      Log(LOG_WARN, "ptt", "PTT ON inhibited locally: TX power is 0%%");

      return;
   }
   ws_send_ptt_cmd_in_room(connection, vfo, on, room);
}

extern const char *configs[];   // from defcfg.c
extern const int num_configs;
extern bool cfg_ui_bell_chat;

extern void connman_autoconnect(void);
extern void rrclient_register_events(void);
extern bool audio_init(void);   // rrclient/audio.c
extern void webcam_client_register_events(void);   // webcam.c
extern bool rrclient_autoconnect(void);
extern void rrclient_poll_events(void);
extern void rrclient_poll_events_reconnect(void);
extern void ws_client_init(void);
extern bool parse_chat_input_real(const char *msg);   // cmd.c
#include <rrclient/cmd.h>
extern bool cfg_servers_init(void) __attribute__((weak));   // cfg.servers.c (optional:
                                                            // IRC server list)
extern bool cfg_network_save_init(void);  // cfg.network.c
extern const char *config_file; // librustyaxe/config.c
extern bool tui_over_ssh;       // librustyaxe/tui.c
// GTK (and any other graphical frontend) lives in a dynamically loaded
// module; core rrclient never links a GUI toolkit.
static bool force_tui = false;  // -T: never load a frontend module
static rr_module_t *frontend_module;   // set once the module loads
struct timespec mono_now;
bool rrclient_cleanup(void);
const char *cfg_log_audio = NULL;
bool cfg_mirc_colors = true;
bool cfg_ui_gtk_vfo_on_top = true;
int cfg_tick_interval = 100;
bool dying = false;
bool restarting = false;
int cfg_ui_vfo_viscosity = -1;
int cfg_ui_edit_delay = 3;      // Seconds to suppress freq updates after local edit
int cfg_ui_ptt_ack_timeout = 2; // Seconds to wait for a PTT ack before reverting
// (gtk.ptt-btn.c)
int cfg_ui_ptt_hold_delay = 500; // Milliseconds before a PTT shortcut is treated as
                                 // hold-to-talk
time_t now = 0;
time_t poll_block_delay = 0;    // CAT polling suppression delay in seconds

/* Keep all long-lived client configuration mirrors in one place.  This is called once after startup config has loaded and again after every complete config
 * reload. */
static bool rrclient_config_refresh(const char *key) {
   (void)key;
   const char *log_audio = cfg_get_exp("log.audio");
   free((void *)cfg_log_audio);
   cfg_log_audio = log_audio;
#ifdef USE_GSTREAMER

   if (cfg_log_audio) {
#ifdef _WIN32
      SetEnvironmentVariable("GST_DEBUG", cfg_log_audio);
      SetEnvironmentVariable("GST_DEBUG_DUMP_DOT_DIR", ".");
#else
      setenv("GST_DEBUG", cfg_log_audio, 1);
      setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 1);
#endif
   }
#endif

   cfg_ui_vfo_viscosity = cfg_get_int("ui.vfo.visocity", 1000);
   poll_block_delay = (cfg_ui_vfo_viscosity > 0) ? (cfg_ui_vfo_viscosity / 1000) : 0;
   cfg_ui_edit_delay = cfg_get_int("ui.edit-delay", 3);
   cfg_ui_ptt_ack_timeout = cfg_get_int("ui.ptt-ack-timeout", 2);
   cfg_ui_ptt_hold_delay = cfg_get_int("ui.ptt-hold-delay", 500);

   if (cfg_ui_ptt_hold_delay < 0) {
      cfg_ui_ptt_hold_delay = 0;
   }
   cfg_ui_bell_chat = cfg_get_bool("ui.bell.chat", false);
   cfg_tick_interval = cfg_get_int("core.tick-interval", 100);

   /* This setting is shared by the GTK and TUI input implementations. */
   tui_set_shared_input_history(cfg_get_bool("ui.shared-input-history", true));

   Log(LOG_DEBUG, "main", "Refreshed cached client configuration");

   return true;
}

static gboolean ws_poll_cb(gpointer user_data) {
   if (dying) {
      return G_SOURCE_REMOVE;
   }
   rrclient_poll_events();
   // GTK and TUI share the nonblocking transport sweep and reconnect engine.
   rrclient_poll_events_reconnect();

   return G_SOURCE_CONTINUE;
}

/*
 * Periodic fwdsp housekeeping shared by both GTK and TUI modes.
 */
static gboolean fwdsp_maintenance_cb(gpointer user_data) {
   (void)user_data;

   fwdsp_reap_children();
   fwdsp_sweep_expired();

   return G_SOURCE_CONTINUE;
}

bool ptt_active = false;
time_t poll_block_expire = 0;   // Here we set this to now +
                                // config:cat.poll-blocking to prevent rig
                                // polling from sclearing local controls

static bool rrclient_server_hotkey(tui_window_t *win, unsigned key, unsigned modifiers, void *user_data) {
   (void)key;
   (void)user_data;

   if (modifiers & (TERMKEY_KEYMOD_ALT | TERMKEY_KEYMOD_SHIFT)) {
      return false;
   }

   return rrclient_connection_cycle_status(win && rrclient_window_is_shared(win->title));
}

static void *tui_nick_completion;
static bool rrclient_tui_chat_completion(tui_window_t *win) {
   (void)win;
   extern char input_buf[TUI_INPUTLEN];
   extern int tui_cursor_pos, tui_input_len;
   size_t cursor = (size_t)tui_cursor_pos;
   char *result = client_chat_complete(input_buf, &cursor, &tui_nick_completion);

   if (!result) {
      return false;
   }
   bool fits = strlen(result) < TUI_INPUTLEN;

   if (fits) {
      strcpy(input_buf, result);
      tui_input_len = (int)strlen(result);
      tui_cursor_pos = (int)cursor;
   }
   free(result);

   return fits;
}

static bool rrclient_ptt_hotkey(tui_window_t *win, unsigned key, unsigned modifiers, void *user_data) {
   (void)ui_active_window_name();
   (void)win;
   (void)key;
   (void)modifiers;
   (void)user_data;

   if (frontend_ops() && frontend_ops()->ptt_hotkey_toggle) {
      return frontend_ops()->ptt_hotkey_toggle();
   }

   if (!ws_conn || ws_connected != 1) {
      return true;
   }
   char vfo[2] = {
      vfo_state_get_active(), '\0'
   };
   bool active = vfo_state_get_bool(vfo, "cat.state.ptt", false);
   rrclient_send_ptt_command(ws_conn, vfo, !active, ui_active_window_name());

   return true;
}

void shutdown_app(int signum) {
   if (signum > 0) {
      Log(LOG_INFO, "core", "Shutting down due to signal %d", signum);
   } else {
      Log(LOG_INFO, "core", "Shutting down by user request");
   }
   // Signal the main loop that we are dying
   dying = true;
}

#ifdef USE_MONGOOSE
// GLib owns the wait between sweeps. Blocking in Mongoose here also blocks
// GTK rendering and input; TUI uses this same nonblocking transport callback.
#define MG_POLL_INTERVAL_MS 10

void poll_mongoose_init(void) {
   g_timeout_add_full(G_PRIORITY_HIGH, MG_POLL_INTERVAL_MS, ws_poll_cb, NULL, NULL);
}
#endif // USE_MONGOOSE

// TUI 1hz clock: updates now, refreshes statusbar/clock, handles shutdown.
// Over SSH (SSH_TTY set) the clock is HH:MM, so we only repaint when the
// minute actually changes instead of once a second.
static gboolean tui_clock_cb_real(gpointer user_data) {
   rrclient_media_room_selected(ui_active_window_name());
   now = time(NULL);

   if (dying) {
      rrclient_cleanup();
   rrclient_database_shutdown();

      return G_SOURCE_REMOVE;
   }

   // Over SSH: repaint only when the minute turns over (or on first tick)
   static time_t last_repaint = 0;

   /* SIGWINCH queues a full redraw because terminal I/O is not signal-safe. Consume it before the SSH clock throttle so resized remote terminals do not keep a
    * partially rendered screen until the next minute. */
   if (tui_redraw_if_pending()) {
      last_repaint = now;

      return G_SOURCE_CONTINUE;
   }

   if (tui_over_ssh && last_repaint != 0 && (now / 60) == (last_repaint / 60)) {
      last_repaint = now;

      return G_SOURCE_CONTINUE;
   }
   last_repaint = now;

   tui_window_t *tw = tui_active_window();
   tui_refresh_sb_window();
   tui_refresh_sb_vfo();
   tui_update_status(tw, "%s %s", sb_online, sb_window);
   tui_redraw_clock();

   return G_SOURCE_CONTINUE;
}

static void rrclient_tui_host_log_frame(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;

   if (dying || !data || len < RR_LOGFRAME_HDR_LEN) {
      return;
   }

   const uint8_t *payload = (const uint8_t *)data;
   char subsys[sizeof(((struct rr_logframe *) 0)->subsys) + 1];
   memcpy(subsys, payload + 1, sizeof(subsys) - 1);
   subsys[sizeof(subsys) - 1] = '\0';

   size_t message_len = strnlen((const char *)payload + RR_LOGFRAME_HDR_LEN, len - RR_LOGFRAME_HDR_LEN);

   if (message_len == 0) {
      return;
   }

   char message[4096];
   size_t src_pos = 0;
   size_t out = 0;
   const unsigned char *src = payload + RR_LOGFRAME_HDR_LEN;
   while (out + 1 < sizeof(message) && src_pos < message_len) {
      unsigned char ch = src[src_pos++];

      if (ch == '\033' && src_pos < message_len && src[src_pos] == '[') {
         // Drop terminal CSI sequences from remote log text.  The TUI adds
         // its own SGR sequences and remote logs must never move the cursor.
         while (src_pos < message_len) {
            unsigned char end = src[src_pos++];

            if (end >= '@' && end <= '~') {
               break;
            }
         }
         continue;
      }

      if (ch < 32 || ch == 127) {
         ch = ' ';
      }
      message[out++] = (char)ch;
   }
   message[out] = '\0';

   ui_print("host log", "%s<%s.%s> %s", get_chat_ts(now), subsys, log_priority_to_str((logpriority_t) payload[0]), message);
}

/* The three TUI system tabs share all servers; ui_print labels each source. GTK has its own log callbacks. */
static bool rrclient_tui_log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   static bool log_printing = false;

   /* tui_vprint redraws the top line.  The top-line renderer reads config, and cfg_get() logs at verbose levels, so allowing that callback back into this
    * function recurses until the stack is exhausted. */
   if (log_printing) {
      return true;
   }

   if (!fmt || dying || debug_filter(subsys, priority)) {
      return true;
   }

   char message[4096];
   va_list copy;
   va_copy(copy, ap);
   vsnprintf(message, sizeof(message), fmt, copy);
   va_end(copy);

   // Log messages can originate in subprocesses and may contain carriage
   // returns or terminal escapes.  Keep them as one safe TUI line.
   for (size_t i = 0 ; message[i] ; i++) {
      unsigned char ch = (unsigned char)message[i];

      if (ch < 32 || ch == 127) {
         message[i] = ' ';
      }
   }

   log_printing = true;
   ui_print("client log", "%s<%s.%s> %s", get_chat_ts(now), subsys ? subsys : "core", log_priority_to_str(priority), message);
   log_printing = false;

   return false;
}

struct talk_msg_event_data {
   char from[128];
   char data[4096];
   char target[128];
   char msg_type[32];
   time_t ts;
};

static void rrclient_handle_talk_msg_event(const char *event, void *data, rrconn_t *cptr, void *user) {
   struct talk_msg_event_data *tmed = (struct talk_msg_event_data *)data;

   if (!tmed || !tmed->from[0] || !tmed->data[0]) {
      return;
   }

   if (strcasecmp(tmed->msg_type, "action") == 0) {
      ui_print(tmed->target[0] ? tmed->target : NULL, "%s\00309* \00311%s\017 %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
   } else {
      ui_print(tmed->target[0] ? tmed->target : NULL, "%s\00314<\00310%s\00314>\017 %s\017", get_chat_ts(tmed->ts), tmed->from, tmed->data);
   }
}

bool rrclient_cleanup(void) {
   // Idempotent: this is reachable from the timeouts, the signal handler, and
   // after gtk_main()/g_main_loop_run() return.  Guard so GTK is not torn down
   // twice (which triggers gtk_main_quit "main_loops != NULL").
   static bool cleaned_up = false;

   if (cleaned_up) {
      return true;
   }
   cleaned_up = true;
   rrclient_identity_shutdown();
   rrclient_exec_shutdown();
   rrclient_quit_servers("Client exiting");
   rrclient_flush_quit();
   tui_set_completion_handler(NULL);
   client_chat_completion_free(tui_nick_completion);
   tui_nick_completion = NULL;

   tui_over_ssh = tui_is_over_ssh();

   // Let the frontend module tear down first: it must unregister its event
   // tokens and idle/timer sources before the event bus goes away.
   if (frontend_module) {
      rr_unload_module(frontend_module->mod_name);
      frontend_module = NULL;
   }

   rrclient_receiver_connect_shutdown();
   receiver_directory_shutdown();

   // Stop all fwdsp children while their Mongoose wrappers and logging are
   // still alive. This also releases encoders retained by fwdsp.hangtime.
   fwdsp_fini();

   // Shut down sockets
#ifdef USE_MONGOOSE
   // NOTE: this fires the "disconnected" event, whose handlers still read
   // cfg (e.g. ui.auto-show-userlist), so cfg must be freed AFTER this.
   ws_fini(&mgr);
#endif // defined(USE_MONGOOSE)

   connman_shutdown();
   cat_pty_shutdown();
   event_shutdown();

   // Persist the running config (including window placements learned while
   // we ran) if ui.save-on-exit says so. Backed up via cfg_save's .old logic.
   if (cfg && config_file && cfg_get_bool("ui.save-on-exit", false)) {
      Log(LOG_INFO, "config", "ui.save-on-exit: saving config to %s", config_file);
      rrclient_database_config_save( config_file);
   }

   free((void *)cfg_log_audio);
   cfg_log_audio = NULL;
   cfg_fini();

   if (ui_mode == UI_MODE_TUI) {
      tui_fini();
   }

   logger_end();
   exit(0);

   return false;
}

void show_arg_help(int argc, char **argv) {
   printf("%s [-T] [-f config] [-s server] [-S] [-h]\n", argv[0]);
   printf("\t-S\t\tssh mode (implies -T; throttle screen updates)\n");
   printf("\t-T\t\tTUI only mode (no X11)\n");
   printf("\t-f config\tChose an alternative configuration file\n");
   printf("\t-s server\tServer profile to autoconnect to on start\n");
   printf("\t-h\t\tHelp\n");
}

////////////////////////////
int main(int argc, char *argv[]) {
   char *display = getenv("DISPLAY");
   char *autoconnect_server = NULL; // -s: server profile to connect to on start

   // Apply the hard-coded defaults from defconfig.c FIRST, so keys missing
   // from the user's config fall back to them.
   if (!default_cfg) {
      default_cfg = dict_new();
   }
   cfg_set_defaults(default_cfg, defcfg);


   int c;
   int digit_optind = 0;

   cfg = default_cfg;

#ifdef USE_COREDUMPS_CLIENT
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
#endif // USE_COREDUMPS_CLIENT

   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();

   // The [modules] config section (after cfg_load) decides which frontend
   // modules to load; until then assume TUI.
   ui_mode = UI_MODE_TUI;

   // Let's do commandline parsing here
   // -T: Always force TUI (no X11)
   while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
         {
            "config", required_argument, 0, 'f'
         },
         {
            "tui", no_argument, 0, 'T'
         },
         {
            "server", required_argument, 0, 's'
         },
         {
            "ssh", no_argument, 0, 'S'
         },
         {
            "help", no_argument, 0, 'h'
         },
         {
            0, 0, 0, 0
         }
      };

      c = getopt_long(argc, argv, "ThSf:s:", long_options, &option_index);

      if (c == -1) {
         break;
      }

      switch (c) {
         case 'f': {
            printf("Using config file: %s\n", optarg);
            config_file = strdup(optarg);
            break;
         }

         case 'S': {
            printf("Setting ssh mode!\n");
            tui_over_ssh = true;
            ui_mode = UI_MODE_TUI;
            break;
         }

         case 'h': {
            show_arg_help(argc, argv);
            exit(0);
            break;
         }

         case 'T': {
            ui_mode = UI_MODE_TUI;
            force_tui = true;
            break;
         }

         case 's': {
            printf("Autoconnect server: %s\n", optarg);
            autoconnect_server = strdup(optarg);
            break;
         }

         case '?': {
            break;
         }

         default: {
            printf("?? getopt returned character code 0%o ??\n", c);
         }
      }
   }

   if (optind < argc) {
      printf("non-option ARGV-elements: ");
      while (optind < argc) {
         printf("%s ", argv[optind++]);
      }
      printf("\n");
   }
   event_init();
   host_init();

   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);
   config_fwdsp_init();

   if (!rr_serial_config_register()) {
      return EXIT_FAILURE;
   }
   cfg_modules_init();

   // Register config save callbacks so module-owned sections get saved.
   // cfg_servers_init() is weak: it lives in cfg.servers.c which is part of
   // the old IRC transport path and may be disabled in rules.mk.
   if (cfg_servers_init) {
      cfg_servers_init();
   }
   cfg_network_save_init();

   if (!rrclient_config_load()) {
      Log(LOG_CRIT, "config", "Unable to load client configuration");
      exit(1);
   }
#ifndef _WIN32
   const char *module_dir = cfg_get("path.modules");
   struct stat module_stat;

   if (!module_dir || module_dir[0] != '/' || stat(module_dir, &module_stat) != 0 || !S_ISDIR(module_stat.st_mode)) {
      Log(LOG_CRIT, "modules", "Invalid installed module directory '%s'. Install rrclient modules and set path.modules to an existing absolute directory.",
         module_dir ? module_dir : "(unset)");

      return EXIT_FAILURE;
   }
#endif

   printf("Loading config %s\n", config_file);

   // -s overrides server.auto-connect so we connect to the given profile on start
   if (autoconnect_server) {
      dict_add(cfg, "server.auto-connect", autoconnect_server);
      free((void *)autoconnect_server);
      autoconnect_server = NULL;
   }

   // apply some global configuration
   char *logfile = cfg_get_path("log.file");
   const char *log_target = logfile ? logfile : "-";

   // "-" means stdout for GTK. Other modes use stdout for their UI, so route
   // the common setting to the normal client log file instead.
   if (ui_mode != UI_MODE_GTK && strcmp(log_target, "-") == 0) {
      log_target = "rrclient.log";
   }
   logger_init(log_target, (ui_mode == UI_MODE_TUI));
   if (!rrclient_database_init() || !rrclient_database_load_profiles()) {
      Log(LOG_CRIT, "db", "Unable to initialize client database");
      return EXIT_FAILURE;
   }

   if (logfile) {
      free(logfile);
      logfile = NULL;
   }

   /////////////////////////////////////////
// Store some oft used config settings //
/////////////////////////////////////////
   reload_event_add(NULL, rrclient_config_refresh, "refresh cached client settings after config reload");
   rrclient_config_refresh(NULL);
   Log(LOG_DEBUG, "main", "CAT poll blocking delay: %d second(s)", (int)poll_block_delay);

   // CAT parsers and configured PTY/serial service bindings
   rr_cat_init();

//////////////////////////////

#ifdef USE_MONGOOSE
   mg_mgr_init(&mgr);
#endif


   // Setup the tab complete and hotkeys
   /* Shared completion provider is used by both GTK and TUI. Keep its registration, but defer all TUI-only callbacks until TUI is selected. */
   tui_register_completion_provider(client_cmd_completions);

   // Reap exited children and expire warm encoders after fwdsp.hangtime.
   // This is independent of the UI and Mongoose polling mechanisms.
   g_timeout_add(1000, fwdsp_maintenance_cb, NULL);

   // Setup stdio & clock: the frontend module must be chosen first, so
   // the TUI setup below runs only when no graphical frontend loaded.
   // The [modules] config section decides what to load, e.g.:
   //    [modules]
   //    rrclient-gtk.so=
   const char *module_options = NULL;
   const char *requested = force_tui ? NULL : cfg_modules_get(0, &module_options);
   const char *loaded_name = NULL;

   if (requested) {
      Log(LOG_INFO, "core", "Requested module %s (path.modules=%s)", requested, cfg_get("path.modules"));

      if (rr_load_module(requested)) {
         frontend_module = rr_find_loaded_module(requested);
         loaded_name = requested;

         if (frontend_module) {
            // init (e.g. gtk_init) needs the real argc/argv; a failed init
            // (no display, theme problems) unloads the module and we fall
            // back to TUI.
            if (frontend_ops()->init(&argc, &argv)) {
               Log(LOG_CRIT, "core", "Frontend module init failed; falling back to TUI");
               rr_unload_module(requested);
               frontend_module = NULL;
               loaded_name = NULL;
            } else {
               ui_mode = UI_MODE_GTK;
#ifdef USE_MONGOOSE
               poll_mongoose_init();   // Nonblocking transport sweeps via GLib
#endif
            }
         }
      } else {
         Log(LOG_INFO, "core", "Requested module %s not found (path.modules=%s); TUI mode", requested, cfg_get("path.modules"));
      }
   } else {
      Log(LOG_INFO, "core", "No modules configured ([modules] section); TUI mode");
   }
   (void)module_options;
   (void)loaded_name;

   if (ui_mode == UI_MODE_TUI) {
      tui_set_completion_handler(rrclient_tui_chat_completion);
      tui_set_completion_describer(client_cmd_completion_describe);
      tui_hotkey_register(TERMKEY_SYM_TAB, TERMKEY_KEYMOD_CTRL, rrclient_server_hotkey, NULL);
      tui_hotkey_register(TERMKEY_SYM_ENTER, TERMKEY_KEYMOD_ALT, rrclient_ptt_hotkey, NULL);
      tui_hotkey_register(' ', TERMKEY_KEYMOD_CTRL, rrclient_ptt_hotkey, NULL);
      tui_hotkey_register(0, TERMKEY_KEYMOD_CTRL, rrclient_ptt_hotkey, NULL);
      tui_input_blocked_cb = rrclient_chat_input_blocked;
      tui_readline_cb = parse_chat_input_real;
      tui_set_topline_renderer(rrclient_tui_topline);
      tui_set_prompt_renderer(rrclient_tui_prompt);
      tui_init();
      // One shared tab per system output type, independent of server count.
      tui_window_create("host log");
      tui_window_create("client log");
      event_on_binary("media.frame.log", rrclient_tui_host_log_frame, NULL);
      log_add_callback(rrclient_tui_log_print_va, LOG_CALLBACK_FILTERED);

      // 1hz TUI clock (statusbar/clock refresh, shutdown check)
      g_timeout_add(1000, tui_clock_cb_real, NULL);
      // 20hz reconnect/poll sweep
      g_timeout_add(rr_rtp_supported(false) ? 10 : 50, ws_poll_cb, NULL);
   }

   // Register all of our core event handlers
   rrclient_register_events();
   rrclient_receiver_connect_init();
   webcam_client_register_events();
   connman_register_events();

   // GStreamer audio paths (RX pipeline feeds from media.frame.audio events;
   // TX pipeline pushes mic samples to the server)
   audio_init();

   // setup the client bits and autoconnect if configured
   ws_client_init();
   connman_autoconnect();

   // start the main loop (both modes now run on GLib)
   if (ui_mode == UI_MODE_TUI) {
      // TUI: run the GLib main loop. The keyboard, clock, and reconnect
      // polling are all GLib sources now (stdin is watched via tui.keys.c)
      GMainLoop *tui_loop = g_main_loop_new(NULL, FALSE);
      g_main_loop_run(tui_loop);
   } else if (frontend_module) {
      // The frontend module owns gtk_main(); its init() built the UI and its
      // run() below returns only when the loop ends.
      frontend_ops()->run();
   }
   rrclient_cleanup();

   return 0;
}
