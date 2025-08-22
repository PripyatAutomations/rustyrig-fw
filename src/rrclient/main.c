//
// src/rrclient/main.c: Core of the client
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "../ext/libmongoose/mongoose.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ui.h"
#include "rrclient/ws.h"

extern bool cfg_detect_and_load(void);
extern bool ws_audio_init(void);
extern struct mg_mgr mgr;
bool dying = false;             // Are we shutting down?
bool restarting = false;        // Are we restarting?
time_t now = -1;                // time() called once a second in main loop to update
bool ptt_active = false;
time_t poll_block_expire = 0;	// Here we set this to now + config:cat.poll-blocking to prevent rig polling from sclearing local controls
time_t poll_block_delay = 0;	// ^-- stores the delay

void shutdown_app(int signum) {
   if (signum > 0) {
      Log(LOG_INFO, "core", "Shutting down due to signal %d", signum);
   } else {
      Log(LOG_INFO, "core", "Shutting down by user request");
   }

   // Signal the main loop that we are dying
   dying = true;
}

// For polling mongoose from glib
static gboolean poll_mongoose(gpointer user_data) {
   mg_mgr_poll(&mgr, 0);
   return G_SOURCE_CONTINUE;
}

/////////////
// 1hz periodic: Check if dying and shutdown, update now variable
/////////////
static gboolean update_now(gpointer user_data) {
   now = time(NULL);

   if (dying) {
      // we should handle local shutdown here
      gtk_main_quit();
      return G_SOURCE_REMOVE;  // remove this timeout
   }
   return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();
   cfg_detect_and_load();
   logger_init();
   host_init();

   // AUDIO debugging
   const char *cfg_debug_audio = cfg_get_exp("audio.debug");
   if (cfg_debug_audio) {
#ifdef _WIN32
      SetEnvironmentVariable("GST_DEBUG", cfg_debug_audio);
#else
      setenv("GST_DEBUG", cfg_debug_audio, 0);
#endif
   }
   free((void *)cfg_debug_audio);
   cfg_debug_audio = NULL;

   /////////////////////////////////////////
   gtk_init(&argc, &argv);



#ifdef _WIN32
   // Disable edit mode in the console, so copy/paste is more usable
   disable_console_quick_edit();

   // see if windows is in dark mode
   win32_check_darkmode();

   // Set the path for gstreamer dump directory
   SetEnvironmentVariable("GST_DEBUG_DUMP_DOT_DIR", ".");
#else
   setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
#endif
   g_timeout_add(1000, update_now, NULL);   // 1hz periodic timer
   g_timeout_add(10, poll_mongoose, NULL);  // Poll Mongoose every 10ms

   gui_init();
   ws_init();

   // How long to suppress hamlib/etc polling during CAT control?  
   int cfg_poll_block_delay = cfg_get_int("cat.poll-blocking", 2);

   // Should we connect to a server on startup?
   const char *autoconnect = cfg_get_exp("server.auto-connect");
   if (autoconnect) {
      memset(active_server, 0, sizeof(active_server));
      snprintf(active_server, sizeof(active_server), "%s", autoconnect);
      ui_print("* Autoconnect set to profile: %s *", active_server);
      connect_or_disconnect(GTK_BUTTON(conn_button));
   } else {
      show_server_chooser();
   }
   free((void *)autoconnect);
   autoconnect = NULL;

   // start gtk main loop
   gtk_main();

   // Cleanup
   ws_fini();
   return 0;
}
