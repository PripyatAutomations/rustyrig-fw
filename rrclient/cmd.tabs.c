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
#include <rrclient/rooms.h>
#include <rrclient/media.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;


bool cmd_admin(int argc, char **args) {
   if (frontend_ops() ) {
      frontend_ops()->focus_tab("admin");
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_config(int argc, char **args) {
   if (frontend_ops() ) {
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
   ui_print(ui_active_window_name(), "\00304/editcfg is only available in the GTK UI\017");

   return false;
}

bool cmd_log(int argc, char **args) {
   if (frontend_ops() ) {
      frontend_ops()->focus_tab("log");
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_win(int argc, char **args) {
   if (argc < 2 || !args || !args[1]) {
      ui_print(ui_active_window_name(), "Usage: /win <number|close [-force]>");

      return true;
   }

   if (!strcasecmp(args[1], "close")) {
      bool force = argc == 3 && !strcasecmp(args[2], "-force");

      if (argc != 2 && !force) {
         ui_print(ui_active_window_name(), "Use /win close [-force] to close the current window");

         return true;
      }

      if (ui_status_active()) {
         ui_print(NULL, "The status window cannot be closed");

         return true;
      }
      const char *active = ui_active_window_name();

      if (!active || !*active || !strcasecmp(active, "status")) {
         ui_print(NULL, "Select a conversation window to close");

         return true;
      }
      char *room = strdup(active);

      if (!room) {
         return true;
      }
      bool error;

      if ((room[0] == '#' || room[0] == '&') && ws_connected == 1 && ws_conn) {
         char part_command[] = "part";
         char *part_args[] = { part_command, room };
         /* The self-PART confirmation performs room/media/tab cleanup. */
         error = cmd_part(2, part_args);
         if (force) {
            rrclient_media_room_parted(room);
            rrclient_room_part(room);
            error = ui_close_window(room);
         }
      } else {
         rrclient_room_part(room); /* Also forget offline reconnect intent. */
         error = ui_close_window(room);
      }
      free(room);

      return error;
   }

   if (ui_mode == UI_MODE_TUI) {
      int id = atoi(args[1]);

      ui_print(ui_active_window_name(), "ID: %s", args[1]);

      if (id < 1 || id > TUI_MAX_WINDOWS) {
         ui_print(ui_active_window_name(), "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);

         return true;
      }
      tui_window_focus_id(id);
   } else if (frontend_ops() ) {
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
