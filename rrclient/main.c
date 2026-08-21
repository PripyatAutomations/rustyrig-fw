//
// rrclient/main.c: Core of the client
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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
#include <ev.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#define	MAX_WINDOWS 32
#define	INPUT_HISTORY_MAX 64
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>

#ifdef USE_MONGOOSE
extern struct mg_mgr mgr;
#endif // defined(USE_MONGOOSE)

extern const char *configs[];  // from defcfg.c
extern const int num_configs;
extern char *config_file;

extern void connman_autoconnect(void);
extern bool ws_audio_init(void);
extern void rrclient_register_events(void);
extern bool rrclient_autoconnect(void);
extern bool rrclient_connect(const char *url);
extern bool rrclient_disconnect(void);
extern void rrclient_poll_events(void);
extern void ws_client_init(void);
extern bool parse_chat_input_real(const char *msg); // cmd.c

/////////////////////////////////////
#ifdef	USE_LIBEV
static ev_timer tui_clock_watcher;
static ev_timer ws_poll_watcher;
struct ev_loop *loop_main = NULL;
#endif	// USE_LIBEV

bool rrclient_cleanup(void);
bool cfg_mirc_colors = true;
bool dying = false;
bool restarting = false;
bool debug_sockets = false;
time_t now = 0;

#ifdef	USE_LIBEV
static void ws_poll_cb(EV_P_ ev_timer *w, int revents) {
   // this calls mg_mgr_poll(&mgr, 0);
   rrclient_poll_events();
}
#endif	// USE_LIBEV
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

////////////////////////////////////////////////////////////////////
// 1hz periodic: Check if dying and shutdown, update now variable //
////////////////////////////////////////////////////////////////////
#ifdef USE_GTK
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
static gboolean poll_mongoose(gpointer user_data) {
   rrclient_poll_events();

   return G_SOURCE_CONTINUE;
}
#endif // USE_MONGOOSE
#endif // USE_GTK

#ifdef	USE_LIBEV
static void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

static void tui_clock_cb(EV_P_ ev_timer *w, int revents) {
   now = time(NULL);

   if (dying) {
      rrclient_cleanup();
   }
   tui_redraw_clock();
}

static void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 0, 1.0);  // start after 0s,
                                                             // repeat every 1s
   ev_timer_start(loop, &tui_clock_watcher);
}
#endif	// USE_LIBEV

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
      ui_print(NULL, "%s * %s %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
   } else {
      ui_print(NULL, "%s {bright-black}<{bright-cyan}%s{bright-black}>{reset} %s{reset}", get_chat_ts(tmed->ts),
         tmed->from, tmed->data);
   }
}

bool rrclient_cleanup(void) {
   logger_end();
   dict_free(cfg);

   if (ui_mode == UI_MODE_TUI) {
      tui_raw_mode(false);
   } else if (ui_mode == UI_MODE_GTK) {
      gtk_main_quit();
   }

   // Shut down sockets
#ifdef USE_MONGOOSE
   ws_fini(&mgr);
#endif // defined(USE_MONGOOSE)

   exit(0);

   return false;
}

void show_help(int argc, char **argv) {
   printf("%s [-T] [-f config] [-h]\n", argv[0]);
   printf("\t-T\t\tTUI only mode (no X11)\n");
   printf("\t-f config\tChose an alternative configuration file\n");
   printf("\t-h\t\tHelp\n");
}

////////////////////////////
int main(int argc, char *argv[]) {
   char *display = getenv("DISPLAY");
   char *fullpath = NULL;

#ifdef USE_LIBEV
   loop_main = EV_DEFAULT;
#endif	// USE_LIBEV
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
#ifdef USE_LIBEV)
      tui_stop_clock_timer(loop_main);
#endif
   struct rlimit rl = {
      0, 0
   };
   setrlimit(RLIMIT_CORE, &rl);
#endif // USE_COREDUMPS_CLIENT

   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();

   // set a default based on if $DISPLAY is set
   if (display) {
      ui_mode = UI_MODE_GTK;
   } else {
      ui_mode = UI_MODE_TUI;
   }

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
            "help", no_argument, 0, 'h'
         },
         {
            0, 0, 0, 0
         }
      };

      c = getopt_long(argc, argv, "Thf:", long_options, &option_index);

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
            show_help(argc, argv);
            exit(0);
            break;
         }

         case 'T': {
            ui_mode = UI_MODE_TUI;
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

   if (config_file) {
      if ( !( cfg = cfg_load(config_file) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
         free(config_file);
         config_file = NULL;
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

   // apply some global configuration
   const char *logfile = cfg_get_exp("log.file");
   logger_init( (logfile ? logfile : "-") );

   if (logfile) {
      free( (char *)logfile );     // _exp versions MUST be freed
      logfile = NULL;
   }

/////////////////////////////////////////
// Store some oft used config settings //
/////////////////////////////////////////
   debug_sockets = cfg_get_bool("debug.sockets", false);
   cfg_fullscreen = cfg_get_bool("ui.full-screen", false);
   const char *cfg_debug_audio = cfg_get_exp("debug.audio");
   // How long to suppress hamlib/etc polling during CAT control?
   int cfg_poll_block_delay = cfg_get_int("cat.poll-blocking", 2);

   if (cfg_debug_audio) {
      // Set the GST_DEBUG environment variable, before spawning subprocesses
#ifdef _WIN32
      SetEnvironmentVariable("GST_DEBUG", cfg_debug_audio);
      // Set the path for gstreamer dump directory
      SetEnvironmentVariable("GST_DEBUG_DUMP_DOT_DIR", ".");
#else
      setenv("GST_DEBUG", cfg_debug_audio, 0);
      setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
#endif
   }
   free( (void *)cfg_debug_audio );
   cfg_debug_audio = NULL;

//////////////////////////////

#ifdef USE_MONGOOSE
   mg_mgr_init(&mgr);
#endif

   // Setup stdio & clock
   if (ui_mode == UI_MODE_TUI) {
      tui_readline_cb = parse_chat_input_real;

      tui_init();
#ifdef	USE_LIBEV
      tui_start_clock_timer(loop_main);
#endif
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef USE_GTK
      g_timeout_add(1000, update_now, NULL);    // 1hz periodic timer
#ifdef USE_MONGOOSE
      g_timeout_add(20, poll_mongoose, NULL);   // Poll Mongoose every 20ms
#endif // defined(USE_MONGOOSE)

      gtk_init(&argc, &argv);

#ifdef _WIN32
      // Disable edit mode in console, so copy/paste is usable
      disable_console_quick_edit();

      // see if windows is in dark mode
      win32_check_darkmode();
#endif // _WIN32
      gui_init();
#endif // defined(USE_GTK)
   }
   alert_dialogs_init();

   // Register all of our core event handlers
   rrclient_register_events();
   connman_register_events();

   ws_client_init();
   connman_autoconnect();

   // start gtk main loop
   if (ui_mode == UI_MODE_TUI) {
      // Here we run the TUI main loop
#ifdef	USE_LIBEV
      ev_timer_init(&ws_poll_watcher, ws_poll_cb, 0, 0.05);
      ev_timer_start(loop_main, &ws_poll_watcher);
      ev_run(loop_main, 0);
#endif	// USE_LIBEV
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef USE_GTK
      gtk_main();
#endif
   }
   rrclient_cleanup();

   return 0;
}
