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
#define MAX_WINDOWS 32
#define INPUT_HISTORY_MAX 64

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>

#if     defined(USE_MONGOOSE)
extern struct mg_mgr mgr;
#endif // defined(USE_MONGOOSE)

extern const char *configs[]; // from defcfg.c
extern const int num_configs;
extern char *config_file;
extern void connman_autoconnect(void);
extern bool ws_audio_init(void);
extern void rrclient_register_events(void);
extern bool tui_input_cb(const char *input);
extern bool rrclient_autoconnect(void);
extern bool rrclient_connect(const char *url);
extern bool rrclient_disconnect(void);
extern void rrclient_poll_events(void);
extern void ws_client_init(void);
static ev_timer tui_clock_watcher;
static ev_timer ws_poll_watcher;
struct ev_loop *loop = NULL;
struct GlobalState rig;
bool rrclient_cleanup(void);
bool mirc_colors = true;
bool ui_mode_gui = true;
bool dying = false;
bool restarting = false;
bool debug_sockets = false;
time_t now = 0;

static void ws_poll_cb(EV_P_ ev_timer *w, int revents) {
   (void)w; (void)revents;
   rrclient_poll_events();
}

bool ptt_active = false;
time_t poll_block_expire = 0;   // Here we set this to now +
                                // config:cat.poll-blocking to prevent rig
                                // polling from sclearing local controls
time_t poll_block_delay = 0;    // ^-- stores the delay

void shutdown_app(int signum) {
   if (signum > 0) {
      Log(LOG_INFO, "core", "Shutting down due to signal %d", signum);
   } else {
      Log(LOG_INFO, "core", "Shutting down by user request");
   }
   // Signal the main loop that we are dying
   dying = true;
}

////////////////////////////////////
// For polling mongoose from glib //
////////////////////////////////////
#if     defined(USE_MONGOOSE)
static gboolean poll_mongoose(gpointer user_data) {
   mg_mgr_poll(&mgr, 0);

   return G_SOURCE_CONTINUE;
}
#endif // defined(USE_MONGOOSE)

////////////////////////////////////////////////////////////////////
// 1hz periodic: Check if dying and shutdown, update now variable //
////////////////////////////////////////////////////////////////////
static gboolean update_now(gpointer user_data) {
   now = time(NULL);
   if (dying) {
      // we should handle local shutdown here
#if     defined(USE_GTK)
      if (ui_mode_gui) {
         gtk_main_quit();
      }
#endif

      return G_SOURCE_REMOVE;  // remove this timeout
   }
   return G_SOURCE_CONTINUE;
}

static void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

static void tui_clock_cb(EV_P_ ev_timer *w, int revents) {
   (void)w; (void)revents;
   if (dying) {
      rrclient_cleanup();
   }
   tui_redraw_clock();
}

static void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 0, 1.0); // start after 0s,
                                                            // repeat every 1s
   ev_timer_start(loop, &tui_clock_watcher);
}

static void rrclient_handle_log_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;

   struct log_event_data *led = (struct log_event_data *)data;
   if (!led || !led->message[0]) {
      return;
   }
   if (!ui_mode_gui) {
      tui_window_t *status = tui_window_find("status");
      if (status) {
         tui_print_win(status, "%s", led->message);
      }
   } else {
      ui_print("<log> %s", led->message);
   }
}

struct talk_msg_event_data {
   char from[128];
   char data[4096];
   char target[128];
   char msg_type[32];
   time_t ts;
};

static void rrclient_handle_talk_msg_event(const char *event, void *data, irc_conn_t *cptr,
                                           void *user) {
   (void)event;
   (void)cptr;
   (void)user;

   struct talk_msg_event_data *tmed = (struct talk_msg_event_data *)data;
   if (!tmed || !tmed->from[0] || !tmed->data[0]) {
      return;
   }
   tui_window_t *wp = tui_active_window();
   if (strcasecmp(tmed->msg_type, "action") == 0) {
      if (!ui_mode_gui) {
         tui_print_win(wp, "%s * %s %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
      } else {
         ui_print("%s * %s %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
      }
   } else {
      if (!ui_mode_gui) {
         tui_print_win(wp, "%s {bright-black}<{bright-cyan}%s{bright-black}>{reset} %s{reset}",
            get_chat_ts(tmed->ts), tmed->from, tmed->data);
      } else {
         ui_print("%s {bright-black}<{bright-cyan}%s{bright-black}>{reset} %s{reset}",
            get_chat_ts(tmed->ts), tmed->from, tmed->data);
      }
   }
}

bool rrclient_cleanup(void) {
   logger_end();
   dict_free(cfg);
   if (!ui_mode_gui) {
      tui_stop_clock_timer(loop);
      tui_raw_mode(false);
   } else {
      // Do stuff here for GTK cleanup
   }
#if     defined(USE_MONGOOSE)
   ws_fini(&mgr);
#endif // defined(USE_MONGOOSE)

   exit(0);

   return false;
}

void show_help(int argc, char **argv) {
   printf("%s [-T] [-c config] [-h]\n", argv[0]);
   printf("\t-T\t\tTUI only mode (no X11)\n");
   printf("\t-c config\tChose an alternative configuration file\n");
   printf("\t-h\t\tHelp\n");
}

////////////////////////////
int main(int argc, char *argv[]) {
   char *display = getenv("DISPLAY");
   char *fullpath = NULL;
   loop = EV_DEFAULT;
   int c;
   int digit_optind = 0;


   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();

   // Let's do commandline parsing here
   // -T: Always force TUI (no X11)
   while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
         {
            "config", required_argument, 0, 'c'
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

      c = getopt_long(argc, argv, "Thc:021", long_options, &option_index);
      if (c == -1) {
         break;
      }
      switch (c) {
         case 'c': {
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
            ui_mode_gui = false;
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
   if (!display) {
      ui_mode_gui = false;
   }
   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);
   if (config_file) {
      if (!(cfg = cfg_load(config_file) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
      }
      free(config_file);
      config_file = NULL;
   } else if ( (fullpath = find_file_by_list(configs, num_configs) ) ) {
      config_file = strdup(fullpath);
      if (!(cfg = cfg_load(fullpath) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      free(fullpath);
   } else {
      // Use default settings and save it to ~/.config/rrclient.cfg
      cfg = default_cfg;
      fprintf(stderr, "No config found :(\n");
      exit(1);
   }
   if ( (fullpath = find_file_by_list(configs, num_configs) ) ) {
      if (fullpath && !(cfg = cfg_load(fullpath) ) ) {
         if (!ui_mode_gui) {
            tui_print_win(tui_window_find("status"),
               "Couldn't load config \"%s\", using defaults instead", fullpath);
         } else {
            ui_print("{red}* ERROR *{reset} Couldn't load config '%s', using defaults", fullpath);
         }
      }
      free(fullpath);
      fullpath = NULL;
   }
   // apply some global configuration
   const char *logfile = cfg_get_exp("log.file");
   logger_init( (logfile ? logfile : "rrclient.log") );
   if (logfile) {
      free( (char *)logfile );    // _exp versions MUST be freed
      logfile = NULL;
   }
   debug_sockets = cfg_get_bool("debug.sockets", false);

   const char *cfg_debug_audio = cfg_get_exp("audio.debug");
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
   // Setup stdio & clock
   if (!ui_mode_gui) {
      tui_readline_cb = tui_input_cb;   // set our input callback
      tui_init();
      tui_print_win(tui_window_find("status"), "rrcli starting");
      tui_start_clock_timer(loop);
#if     defined(USE_GTK)
   } else {
      g_timeout_add(1000, update_now, NULL);   // 1hz periodic timer

#if     defined(USE_MONGOOSE)
      g_timeout_add(10, poll_mongoose, NULL);  // Poll Mongoose every 10ms
#endif // defined(USE_MONGOOSE)
      gtk_init(&argc, &argv);
#ifdef _WIN32
      // Disable edit mode in the console, so copy/paste is more usable
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

   // How long to suppress hamlib/etc polling during CAT control?
   int cfg_poll_block_delay = cfg_get_int("cat.poll-blocking", 2);

   ws_client_init();
//   connman_autoconnect();
   if (ui_mode_gui) {
      // start gtk main loop
      gtk_main();
   } else {
      // Here we run the TUI main loop
#if defined(USE_MONGOOSE)
      mg_mgr_init(&mgr);
#else
      ev_timer_init(&ws_poll_watcher, ws_poll_cb, 0, 0.05);
      ev_timer_start(loop, &ws_poll_watcher);
#endif
      ev_run(loop, 0);
   }
   rrclient_cleanup();

   return 0;
}
