#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "../ext/libmongoose/mongoose.h"
//#include "rustyrig/http.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern bool ws_audio_init(void);
extern struct mg_mgr mgr;
extern struct mg_connection *ws_conn;
extern defconfig_t defcfg[];

const char *config_file = NULL;
int my_argc = -1;
char **my_argv = NULL;
bool dying = false;             // Are we shutting down?
bool restarting = false;        // Are we restarting?
time_t now = -1;                // time() called once a second in main loop to update
bool ptt_active = false;
time_t poll_block_expire = 0;	// Here we set this to now + config:cat.poll-blocking to prevent rig polling from sclearing local controls
time_t poll_block_delay = 0;	// ^-- stores the delay

const char *configs[] = { 
   "~/.config/rrclient.cfg",
   "~/.rrclient.cfg",
   "/etc/rrclient.cfg"
};

void shutdown_app(int signum) {
   if (signum > 0) {
      Log(LOG_INFO, "core", "Shutting down due to signal %d", signum);
   } else {
      Log(LOG_INFO, "core", "Shutting down by user request");
   }
   dying = true;
}

static gboolean poll_mongoose(gpointer user_data) {
   mg_mgr_poll(&mgr, 0);
   return G_SOURCE_CONTINUE;
}

static gboolean update_now(gpointer user_data) {
   now = time(NULL);
   if (dying) {
      gtk_main_quit();
      return G_SOURCE_REMOVE;  // remove this timeout
   }
   return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
   bool empty_config = true;

   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();

   const char *homedir = getenv("HOME");

   // Find and load the configuration file
   int cfg_entries = (sizeof(configs) / sizeof(char *));
   char *realpath = find_file_by_list(configs, cfg_entries);

   // Load the default configuration
   cfg_init(defcfg);

   if (realpath) {
      config_file = strdup(realpath);
      if (!(cfg = cfg_load(realpath))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", realpath);
      } else {
         Log(LOG_DEBUG, "config", "Loaded config from '%s'", realpath);
      }
      empty_config = false;
      free(realpath);
   } else {
     // Use default settings and save it to ~/.config/rrclient.cfg
     cfg = default_cfg;
     empty_config = true;
     Log(LOG_WARN, "core", "No config file found, saving defaults to ~/.config/rrclient.cfg");
   }

   // Set logging configuration to make our loaded config, in production this will quiet logging down
   logger_init();

   host_init();

   // Set up some debugging
   setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
   const char *cfg_audio_debug = cfg_get("audio.debug");
   if (cfg_audio_debug) {
      setenv("GST_DEBUG", cfg_audio_debug, 0);
   }

   ws_init();
   gtk_init(&argc, &argv);
   audio_init();
   ws_audio_init();
   gui_init();
   g_timeout_add(10, poll_mongoose, NULL);
   g_timeout_add(1000, update_now, NULL);

   const char *poll_block_delay_s = cfg_get("cat.poll-blocking");
   int cfg_poll_block_delay = 3;
   if (poll_block_delay_s) {
      poll_block_delay = atoi(poll_block_delay_s);
   }

   // Should we connect to a server on startup?
   const char *autoconnect = cfg_get("server.auto-connect");
   if (autoconnect) {
      memset(active_server, 0, sizeof(active_server));
      snprintf(active_server, sizeof(active_server), "%s", autoconnect);
      connect_or_disconnect(GTK_BUTTON(conn_button));
   } else {
      show_server_chooser();
   }

   char pathbuf[PATH_MAX+1];
   memset(pathbuf, 0, sizeof(pathbuf));

   // If we don't couldnt find a config file, save the defaults to ~/.config/rrclient.cfg
   if (homedir && empty_config) {
      snprintf(pathbuf, sizeof(pathbuf), "%s/.config/rrclient.cfg", homedir);
      if (!file_exists(pathbuf)) {
         Log(LOG_CRIT, "main", "Saving default config to %s since it doesn't exist", pathbuf);
         cfg_save(pathbuf);
         config_file = pathbuf;
      }
   }

   // start gtk main loop
   gtk_main();

   // Cleanup
   ws_fini();
   return 0;
}
