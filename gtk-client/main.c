#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "inc/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern bool ws_audio_init(void);
// config.c
extern bool config_load(const char *path);
extern dict *cfg;
extern struct mg_mgr mgr;
extern struct mg_connection *ws_conn;

int my_argc = -1;
char **my_argv = NULL;
bool dying = 0;                 // Are we shutting down?
bool restarting = 0;            // Are we restarting?
time_t now = -1;                // time() called once a second in main loop to update
bool ptt_active = false;
time_t poll_block_expire = 0;	// Here we set this to now + config:cat.poll-blocking to prevent rig polling from sclearing local controls
time_t poll_block_delay = 0;	// ^-- stores the delay

static const char *configs[] = { 
   "~/.config/rrclient.cfg",
   "~/.rrclient.cfg",
   "/etc/rrclient.cfg",
   "/opt/rustyrig/etc/rrclient.cfg",
};

gboolean check_dying(gpointer data) {
   if (dying) {
      gtk_main_quit();
      return G_SOURCE_REMOVE;  // remove this timeout
   }
   return G_SOURCE_CONTINUE;
}

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
   return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
   logfp = stdout;
   log_level = LOG_DEBUG;

   int cfg_entries = (sizeof(configs) / sizeof(char *));
   const char *realpath = find_file_by_list(configs, cfg_entries);

   if (config_load(realpath)) {
      Log(LOG_CRIT, "core", "Couldn't load config \"%s\", bailing!", realpath);
      exit(1);
   }

   host_init();

   if (cfg == NULL) {
      Log(LOG_CRIT, "core", "Failed to load configuration, bailing!");
      exit(1);
   }

   // Set up some debugging
   setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
   const char *cfg_audio_debug = dict_get(cfg, "audio.debug", ":*3");
   setenv("GST_DEBUG", cfg_audio_debug, 0);

   ws_init();
   gtk_init(&argc, &argv);
   ws_audio_init();
   gui_init();
   g_timeout_add(10, poll_mongoose, NULL);
   g_timeout_add(1000, update_now, NULL);
   g_timeout_add(1000, check_dying, NULL);
   char *poll_block_delay_s = dict_get(cfg, "cat.poll-blocking", "2");
   poll_block_delay = atoi(poll_block_delay_s);

   // Should we connect on startup?
   char *autoconnect = dict_get(cfg, "server.auto-connect", "false");
   if (strcasecmp(autoconnect, "true") == 0 || strcasecmp(autoconnect, "yes") == 0 || strcasecmp(autoconnect, "on") == 0) {
      connect_or_disconnect(GTK_BUTTON(conn_button));
   }


   // start gtk main loop
   gtk_main();

   // Cleanup
   ws_fini();
   return 0;
}
