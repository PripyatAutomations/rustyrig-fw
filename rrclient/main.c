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
#include <getopt.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <librrprotocol/rrprotocol.h>
#include <glib.h>
extern defconfig_t defcfg[];
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#define	MAX_WINDOWS 32
#define	INPUT_HISTORY_MAX 64
#include <rrclient/ui.h>
#include <rrclient/cat.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.statusbar.h>

#ifdef USE_MONGOOSE
extern struct mg_mgr mgr;
#endif // defined(USE_MONGOOSE)

extern const char *configs[];  // from defcfg.c
extern const int num_configs;
extern bool cfg_ui_bell_chat;

extern void connman_autoconnect(void);
extern void rrclient_register_events(void);
extern bool rrclient_autoconnect(void);
extern void rrclient_poll_events(void);
extern void rrclient_poll_events_reconnect(void);
extern void ws_client_init(void);
extern bool parse_chat_input_real(const char *msg); // cmd.c
extern char **client_cmd_completions(const char *line, const char *word); // cmd.c
extern bool cfg_servers_init(void) __attribute__((weak));   // cfg.servers.c (optional: IRC server list)
extern bool cfg_network_save_init(void);  // cfg.network.c

struct timespec mono_now;
bool rrclient_cleanup(void);
const char *cfg_debug_audio = NULL;
bool cfg_mirc_colors = true;
bool cfg_ui_gtk_vfo_on_top = true;
int cfg_tick_interval = 100;
bool dying = false;
bool restarting = false;
bool debug_sockets = false;
int cfg_ui_vfo_viscosity = -1;
int cfg_ui_edit_delay = 3;          // Seconds to suppress freq updates after local edit
int cfg_ui_ptt_ack_timeout = 2;     // Seconds to wait for a PTT ack before reverting (gtk.ptt-btn.c)
time_t now = 0;

static gboolean ws_poll_cb(gpointer user_data) {
   rrclient_poll_events();
   return G_SOURCE_CONTINUE;
}

bool ptt_active = false;
time_t poll_block_expire = 0;    // Here we set this to now +
                                 // config:cat.poll-blocking to prevent rig
                                 // polling from sclearing local controls
time_t poll_block_delay = 0;     // ^-- stores the delay

void shutdown_app(int signum) {
   if (signum > 0) {
      Log(LOG_INFO, "core", "Shutting down due to signal %d", signum);
   } else {
      Log(LOG_INFO, "core", "Shutting down by user request");
   }
   // Signal the main loop that we are dying
   dying = true;
}

#ifdef USE_GTK
int cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM;

////////////////////////////////////////////////////////////////////
// 1hz periodic: Check if dying and shutdown, update now variable //
////////////////////////////////////////////////////////////////////
static gboolean update_now(gpointer user_data) {
   now = time(NULL);

   if (dying) {
      // we should handle local shutdown here
      rrclient_cleanup();
      return G_SOURCE_REMOVE;   // remove this timeout
   }

   return G_SOURCE_CONTINUE;
}

#ifdef USE_MONGOOSE
////////////////////////////////////
// For polling mongoose from glib //
////////////////////////////////////
// Mongoose GSource integration (GTK mode).  Instead of a plain g_timeout_add() we hook a
// custom GSource into the GTK main loop so poll scheduling behaves like a
// real event source: prepare()/check() decide when to dispatch, and
// mg_mgr_poll() gets a small blocking timeout so idle wakeups are cheap.
typedef struct {
   GSource source;
} MgSource;

static gint64 mg_next_poll_us = 0;   // monotonic usec of next due poll

// Cadence of the reconnect/poll sweep. Socket events DON'T wait for this:
// dispatch blocks inside mg_mgr_poll()'s internal poll() and returns
// immediately when data arrives. Keep interval > poll block time or the
// source stays permanently ready and starves GTK.
//
// The 10ms/8ms split keeps the client ~90% blocked in poll() (near-zero CPU)
// while capping event latency at ~10ms. Don't lengthen the interval without
// lengthening the block to match -- GLib sleep time is NOT woken by socket
// data since we register no GPollFDs.
#define	MG_POLL_INTERVAL_US	10000
#define	MG_POLL_BLOCK_MS	8

static gboolean mg_source_prepare(GSource *source, gint *timeout_) {
   gint64 now = g_get_monotonic_time();

   if (now >= mg_next_poll_us) {
      mg_next_poll_us = now + MG_POLL_INTERVAL_US;
      *timeout_ = 0;
      return TRUE;                     // dispatch now
   }

   *timeout_ = (gint)((mg_next_poll_us - now) / 1000);
   return FALSE;
}

static gboolean mg_source_check(GSource *source) {
   // Must stay FALSE: returning TRUE makes the source permanently ready,
   // spinning the main loop at 100% CPU.
   return FALSE;
}

static gboolean mg_source_dispatch(GSource *source, GSourceFunc cb, gpointer data) {
   // Block up to 45ms inside mg_mgr_poll's internal poll(): it returns
   // immediately when socket data arrives, so event latency is unaffected,
   // but idle wakeups drop to ~20/s instead of spinning at 200/s.
   if (!dying) {
#ifdef	USE_MONGOOSE
      mg_mgr_poll(&mgr, MG_POLL_BLOCK_MS);
#endif
      rrclient_poll_events_reconnect();
   }
   return G_SOURCE_CONTINUE;
}

static void mg_source_finalize(GSource *source) {
}

static GSourceFuncs mg_source_funcs = {
   mg_source_prepare, mg_source_check, mg_source_dispatch, mg_source_finalize,
   NULL, NULL
};

static void poll_mongoose_init(void) {
   GSource *src = g_source_new(&mg_source_funcs, sizeof(MgSource));
   g_source_set_name(src, "mongoose-poll");
   // Priority must be above the default-idle band: during UI setup and heavy
   // redraws an idle-priority source is starved, delaying socket reads (and
   // thus ping/pong RTT measurement and eventually audio) by hundreds of ms.
   g_source_set_priority(src, G_PRIORITY_DEFAULT);
   g_source_attach(src, g_main_context_default());
   g_source_unref(src);
}

static gboolean poll_mongoose(gpointer user_data) {
   rrclient_poll_events();
   return G_SOURCE_CONTINUE;
}
#endif // USE_MONGOOSE
#endif // USE_GTK

// TUI 1hz clock: updates now, refreshes statusbar/clock, handles shutdown.
// Over SSH (SSH_TTY set) the clock is HH:MM, so we only repaint when the
// minute actually changes instead of once a second.
static gboolean tui_clock_cb_real(gpointer user_data) {
   now = time(NULL);

   if (dying) {
      rrclient_cleanup();
      return G_SOURCE_REMOVE;
   }

   // Over SSH: repaint only when the minute turns over (or on first tick)
   static time_t last_repaint = 0;
   bool over_ssh = tui_is_over_ssh();

   if (over_ssh && last_repaint != 0 && (now / 60) == (last_repaint / 60) ) {
      last_repaint = now;
      return G_SOURCE_CONTINUE;
   }
   last_repaint = now;

   tui_window_t *tw = tui_active_window();
   tui_refresh_sb_window();
   tui_refresh_sb_vfo();
   tui_update_status(tw, "%s %s %s", sb_online, sb_window, sb_vfo);
   tui_redraw_clock();
   return G_SOURCE_CONTINUE;
}

static void rrclient_handle_log_event(const char *event, void *data, rrconn_t *cptr, void *user) {
   struct log_event_data *led = (struct log_event_data *)data;

   if (!led || !led->message[0]) {
      return;
   }

   ui_print("status", "%s", led->message);
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
      ui_print(NULL, "%s {bright-green}* {bright-cyan}%s{reset} %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
   } else {
      ui_print(NULL, "%s {bright-black}<{cyan}%s{bright-black}>{reset} %s{reset}", get_chat_ts(tmed->ts),
         tmed->from, tmed->data);
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
   logger_end();

   if (ui_mode == UI_MODE_TUI) {
      tui_raw_mode(false);
            } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
          // Only quit if the main loop is still running.  When the user closes
          // the window, destroy→gtk_main_quit already unwound the loop and
          // calling gtk_main_quit() again asserts ("main_loops != NULL").
          if (gtk_main_level() > 0) {
             gtk_main_quit();
          }
#endif	// USE_GTK
       }

   // Shut down sockets
#ifdef USE_MONGOOSE
   // NOTE: this fires the "disconnected" event, whose handlers still read
   // cfg (e.g. ui.auto-show-userlist), so cfg must be freed AFTER this.
   ws_fini(&mgr);
#endif // defined(USE_MONGOOSE)

   dict_free(cfg);

#if defined(USE_LIBNOTIFY) && defined(USE_GTK)
   ui_notify_fini();
#endif	// USE_LIBNOTIFY && USE_GTK

   exit(0);

   return false;
}

void show_arg_help(int argc, char **argv) {
   printf("%s [-T] [-f config] [-s server] [-h]\n", argv[0]);
   printf("\t-T\t\tTUI only mode (no X11)\n");
   printf("\t-f config\tChose an alternative configuration file\n");
   printf("\t-s server\tServer profile to autoconnect to on start\n");
   printf("\t-h\t\tHelp\n");
}

////////////////////////////
int main(int argc, char *argv[]) {
   char *display = getenv("DISPLAY");
   char *fullpath = NULL;
   char *autoconnect_server = NULL;   // -s: server profile to connect to on start

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

   // set a default based on if $DISPLAY is set
#if defined(USE_GTK)
   if (display) {
      ui_mode = UI_MODE_GTK;
   } else {
      ui_mode = UI_MODE_TUI;
   }
#else
   // GTK is not compiled in, always use TUI (-T becomes a no-op)
   ui_mode = UI_MODE_TUI;
#endif

   // Let's do commandline parsing here
   // -T: Always force TUI (no X11)
   while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
         { "config", required_argument, 0, 'f' },
         { "tui", no_argument, 0, 'T' },
         { "server", required_argument, 0, 's' },
         { "help", no_argument, 0, 'h' },
         { 0, 0, 0, 0 }
      };

      c = getopt_long(argc, argv, "Thf:s:", long_options, &option_index);

      if (c == -1) {
         break;
      }

      switch (c) {
         case 'f': {
            printf("Using config file: %s\n", optarg);
            config_file = strdup(optarg);
            break;
         }

         case 'h': {
            show_arg_help(argc, argv);
            exit(0);
            break;
         }

         case 'T': {
            ui_mode = UI_MODE_TUI;
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
#ifdef	USE_GTK
extern bool cfg_gtkcss_init(void);   // cfg.gtkcss.c
   cfg_gtkcss_init();
#endif

   // Register config save callbacks so module-owned sections get saved.
   // cfg_servers_init() is weak: it lives in cfg.servers.c which is part of
   // the old IRC transport path and may be disabled in rules.mk.
   if (cfg_servers_init) {
      cfg_servers_init();
   }
   cfg_network_save_init();

   if (config_file) {
      if ( !( cfg = cfg_load(config_file) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
         free( (void *)config_file);
         config_file = NULL;
         exit(1);
      } else {
         printf("Loading config %s\n", config_file);
      }
   }

   if ( !config_file && ( fullpath = find_file_by_list(configs, num_configs) ) ) {
      config_file = strdup(fullpath);

      if ( !( cfg = cfg_load(fullpath) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      printf("Loading config %s\n", config_file);
      free(fullpath);
   }

   if (!config_file){
      // Use default settings builtin
      fprintf(stderr, "No config found :(\n");
      exit(1);
   }

   // -s overrides server.auto-connect so we connect to the given profile on start
   if (autoconnect_server) {
      dict_add(cfg, "server.auto-connect", autoconnect_server);
      free( (void *)autoconnect_server);
      autoconnect_server = NULL;
   }

   // apply some global configuration
   const char *logfile = cfg_get_exp("log.file");
   logger_init( (logfile ? logfile : "-"), (ui_mode == UI_MODE_TUI) );

   if (logfile) {
      free( (char *)logfile );     // _exp versions MUST be freed
      logfile = NULL;
   }

/////////////////////////////////////////
// Store some oft used config settings //
/////////////////////////////////////////
   debug_sockets = cfg_get_bool("debug.sockets", false);
   cfg_debug_audio = cfg_get_exp("debug.audio");
   // How long to suppress hamlib/etc polling during CAT control?
   // (config is in milliseconds; poll_block_delay is whole seconds)
   cfg_ui_vfo_viscosity = cfg_get_int("ui.vfo.visocity", 1000);
   poll_block_delay = (cfg_ui_vfo_viscosity > 0) ? (cfg_ui_vfo_viscosity / 1000) : 0;
   Log(LOG_DEBUG, "main", "CAT poll blocking delay: %d second(s)", (int)poll_block_delay);
   // How long after a local freq edit to suppress CAT poll echoes (seconds)?
   cfg_ui_edit_delay = cfg_get_int("ui.edit-delay", 3);
   // How long to wait for a PTT ack before reverting the button (seconds)?
   cfg_ui_ptt_ack_timeout = cfg_get_int("ui.ptt-ack-timeout", 2);
   cfg_ui_bell_chat = cfg_get_bool("ui.bell.chat", false);
   cfg_tick_interval = cfg_get_int("core.tick-interval", 100);

   // CAT parsers + PTY interface (./dev/ttyCAT0 when cat.pty.enable is true)
   rr_cat_init();

#ifdef	USE_GTK
   cfg_fullscreen = cfg_get_bool("ui.full-screen", false);
   cfg_ui_gtk_vfo_on_top = cfg_get_bool("ui.gtk.vfo-on-top", true);

   const char *main_tabstrip_s = cfg_get("ui.gtk.main-tabstrip");
   if (main_tabstrip_s && main_tabstrip_s[0] != '\0') {
      if (strcasecmp(main_tabstrip_s, "left") == 0) {
        cfg_ui_gtk_main_tabstrip = GTK_POS_LEFT;
        Log(LOG_DEBUG, "ui.core", "Placing main tabstrip at LEFT");
      } else if (strcasecmp(main_tabstrip_s, "right") == 0) {
        cfg_ui_gtk_main_tabstrip = GTK_POS_RIGHT;
        Log(LOG_DEBUG, "ui.core", "Placing main tabstrip at RIGHT");
      } else if (strcasecmp(main_tabstrip_s, "top") == 0) {
        cfg_ui_gtk_main_tabstrip = GTK_POS_TOP;
        Log(LOG_DEBUG, "ui.core", "Placing main tabstrip at TOP");
      } else if (strcasecmp(main_tabstrip_s, "bottom") == 0) {
        cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM;
        Log(LOG_DEBUG, "ui.core", "Placing main tabstrip at BOTTOM");
      }
   } else {
      Log(LOG_CRIT, "ui.core", "No configuration for ui.gtk.main-tabstrip!");
   }
#endif

   if (cfg_debug_audio) {
#ifdef	USE_GSTREAMER
      // Set the GST_DEBUG environment variable, before spawning subprocesses
#ifdef _WIN32
      SetEnvironmentVariable("GST_DEBUG", cfg_debug_audio);
      // Set the path for gstreamer dump directory
      SetEnvironmentVariable("GST_DEBUG_DUMP_DOT_DIR", ".");
#else	// _WIN32
      setenv("GST_DEBUG", cfg_debug_audio, 0);
      setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
#endif	// _WIN32
#endif	// USE_GSTREAMER
   }
   free( (void *)cfg_debug_audio );
   cfg_debug_audio = NULL;

//////////////////////////////

#ifdef USE_MONGOOSE
   mg_mgr_init(&mgr);
#endif

   tui_register_completion_provider(client_cmd_completions);

   // Setup stdio & clock
   if (ui_mode == UI_MODE_TUI) {
      tui_readline_cb = parse_chat_input_real;
      tui_init();

      // 1hz TUI clock (statusbar/clock refresh, shutdown check)
      g_timeout_add(1000, tui_clock_cb_real, NULL);
      // 20hz reconnect/poll sweep
      g_timeout_add(50, ws_poll_cb, NULL);
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef USE_GTK
      g_timeout_add(1000, update_now, NULL);    // 1hz periodic timer

#ifdef USE_MONGOOSE
      poll_mongoose_init();                     // Mongoose via GSource
#endif // defined(USE_MONGOOSE)

      gtk_init(&argc, &argv);
      log_add_callback(log_print_va);

#ifdef _WIN32
      // Disable edit mode in console, so copy/paste is usable
      disable_console_quick_edit();

      // see if windows is in dark mode
      win32_check_darkmode();
#endif // _WIN32
      gui_init();

#ifdef	USE_LIBNOTIFY
      if (!ui_notify_init()) {
         Log(LOG_WARN, "gtk.notify", "Desktop notifications unavailable");
      }
#endif	// USE_LIBNOTIFY
      alert_dialogs_init();
#endif // USE_GTK
   }

   // Register all of our core event handlers
   rrclient_register_events();
   connman_register_events();

   // setup the client bits and autoconnect if configured
   ws_client_init();
   connman_autoconnect();

   // start the main loop (both modes now run on GLib)
   if (ui_mode == UI_MODE_TUI) {
      // TUI: run the GLib main loop. The keyboard, clock, and reconnect
      // polling are all GLib sources now (stdin is watched via tui.keys.c)
      GMainLoop *tui_loop = g_main_loop_new(NULL, FALSE);
      g_main_loop_run(tui_loop);
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef USE_GTK
      gtk_main();
#endif
   }
   rrclient_cleanup();

   return 0;
}
