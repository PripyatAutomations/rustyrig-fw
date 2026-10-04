//
// rrclient/cmd.tabs.c: Commands related to switching tabs/windows
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;


bool cmd_admin(int argc, char **args) {
   if ( frontend_ops() ) {
      frontend_ops()->focus_tab("admin");
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_config(int argc, char **args) {
   if ( frontend_ops() ) {
      frontend_ops()->focus_tab("config");
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

// Show the edit-config dialog.  GTK only; there is no TUI equivalent.
bool cmd_editcfg(int argc, char **args) {
   if (frontend_ops() && frontend_ops()->edit_config) {
      frontend_ops()->edit_config(argc > 1 ? args[1] : NULL);

      return false;
   }
   ui_print(ui_active_window_name(), "{bright-red}/editcfg is only available in the GTK UI{reset}");

   return false;
}

bool cmd_log(int argc, char **args) {
   if ( frontend_ops() ) {
      frontend_ops()->focus_tab("log");
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_win(int argc, char **args) {
   if (argc < 1) {
      return true;
   }

   if (ui_mode == UI_MODE_TUI) {
      if (strcasecmp(args[1], "close") == 0) {
         Log(LOG_CRIT, "test", "argc: %d args0: %s args1: %s", argc, args[0], args[1]);

         if (argc < 2) {
            return true;
         }
         int id = -1;

         if (argc >= 3) {
            id = atoi(args[2]);
         } else {
            return tui_window_destroy( tui_active_window() );
         }

         if (id > 0) {
            tui_window_destroy_id(id);

            return false;
         }

         return true;
      }
      int id = atoi(args[1]);

      ui_print(ui_active_window_name(), "ID: %s", args[1]);

      if (id < 1 || id > TUI_MAX_WINDOWS) {
         ui_print(ui_active_window_name(), "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);

         return true;
      }
      tui_window_focus_id(id);
   } else if ( frontend_ops() ) {
      // XXX: add window commands (close, etc)
      if (argc < 2) {
         return true;
      }
      int id = atoi(args[1]);

      if (id >= 1) {
         frontend_ops()->switch_window(id);
      } else {
         ui_print(ui_active_window_name(), "Invalid window id %d given", id);

         return true;
      }
   }

   return false;
}
