//
// rrclient/cmd.tabs.c: Commands related to switching tabs/windows
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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
#include <ev.h>

extern bool dying;
extern time_t now;

#ifdef	USE_MONGOOSE
extern struct mg_connection *ws_conn;
#endif	// USE_MONGOOSE

#ifdef	USE_GTK
extern GtkWidget *chat_entry;
extern GtkWidget *config_tab;
extern GtkWidget *main_notebook;
extern GtkWidget *status_tab;
extern GtkWidget *log_tab;
#endif // defined(USE_GTK)

bool cmd_admin(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), admin_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
#endif	// USE_GTK
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_chat(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), status_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         gtk_widget_grab_focus( GTK_WIDGET(chat_entry) );
      }
#endif	// USE_GTK
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_config(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), config_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
#endif	// USE_GTK
   } else if (ui_mode == UI_MODE_TUI) {
   }

   return false;
}

bool cmd_log(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), log_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
#endif	// USE_GTK
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

      ui_print(NULL, "ID: %s", args[1]);

      if (id < 1 || id > TUI_MAX_WINDOWS) {
         ui_print(NULL, "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);

         return true;
      }
      tui_window_focus_id(id);
   }

   return false;
}
