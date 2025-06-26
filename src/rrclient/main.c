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
// This needs to be up here to make sure noone loads windows.h before winsock2.h
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
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

#ifdef _WIN32
extern bool is_windows_dark_mode();
#endif

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
#ifndef _WIN32
   "~/.config/rrclient.cfg",
   "~/.rrclient.cfg",
   "/etc/rrclient.cfg"
#else
   "%APPDATA%\\rrclient\\rrclient.cfg",
   ".\\rrclient.cfg"
#endif
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
   bool empty_config = false;

   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();

   const char *homedir = getenv("HOME");

   // Find and load the configuration file
   int cfg_entries = (sizeof(configs) / sizeof(char *));
   char *realpath = find_file_by_list(configs, cfg_entries);

   // Load the default configuration
   cfg_init(default_cfg, defcfg);

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
#ifdef _WIN32
   disable_console_quick_edit();
   SetEnvironmentVariable("GST_DEBUG_DUMP_DOT_DIR", ".");
#else
   setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
#endif

   const char *cfg_audio_debug = cfg_get("audio.debug");
   if (cfg_audio_debug) {
#ifdef _WIN32
      SetEnvironmentVariable("GST_DEBUG", cfg_audio_debug);
#else
      setenv("GST_DEBUG", cfg_audio_debug, 0);
#endif
   }

   gtk_init(&argc, &argv);

    // Ensure windows dark mode &
#ifdef	_WIN32
   if (is_windows_dark_mode()) {
      GtkSettings *settings = gtk_settings_get_default();
      g_object_set(settings, "gtk-theme-name", "Windows10-Dark",
                             "gtk-application-prefer-dark-theme", TRUE,
                             NULL);
   } else {
      GtkSettings *settings = gtk_settings_get_default();
      g_object_set(settings, "gtk-theme-name", "Windows10",
                             "gtk-application-prefer-dark-theme", FALSE,
                             NULL);
   }
#endif

   g_timeout_add(1000, update_now, NULL);
   g_timeout_add(10, poll_mongoose, NULL);  // Poll Mongoose every 10ms

   gui_init();
   ws_init();
  
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
      ui_print("* Autoconnect set to profile: %s *", active_server);
      connect_or_disconnect(GTK_BUTTON(conn_button));
   } else {
      show_server_chooser();
   }

   char pathbuf[PATH_MAX+1];
   memset(pathbuf, 0, sizeof(pathbuf));

   // If we don't couldnt find a config file, save the defaults to ~/.config/rrclient.cfg
   if (homedir && empty_config) {
#ifdef _WIN32
      snprintf(pathbuf, sizeof(pathbuf), "%%APPDATA%%\\rrclient\\rrclient.cfg");
#else
      snprintf(pathbuf, sizeof(pathbuf), "%s/.config/rrclient.cfg", homedir);
#endif
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
