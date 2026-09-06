//
// rrclient/cmd.misc.c: unsorted commands
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

extern bool dying;
extern time_t now;
extern bool syslog_clear(void);
extern const char *server_name; // remove this (connman.c)
extern rrconn_t *ws_conn;
extern bool ui_confirm_quit(void);

bool cmd_clear(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      tui_clear_scrollback( tui_active_window() );
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      gtk_text_buffer_set_text(text_buffer, "", -1);
#endif
   }

   return false;
}

bool cmd_clearlog(int argc, char **args) {
#ifdef	USE_GTK
   syslog_clear();
#endif
   return false;
}

bool cmd_disconnect(int argc, char **args) {
   disconnect_server(server_name);
   return false;
}


bool cmd_quit(int argc, char **args) {
   const char *quitmsg = "no reason given";

   if (argc > 1 && args && args[1][0] != '\0') {
      quitmsg = args[1];
   }

#ifdef	USE_GTK
   // Confirm before quitting in GTK mode
   if (ui_mode == UI_MODE_GTK && !ui_confirm_quit() ) {
      return false;
   }
#endif	// USE_GTK

   ui_print(NULL, "{bright-cyan}Seeya soon, have a great day!{reset}");

   dict *d = dict_new();
   dict_add(d, "msg.type", "auth");
   dict_add(d, "auth.cmd", "quit");
   dict_add(d, "auth.msg", quitmsg);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   // Set the dying flag so main loop with cleanly exit
   dying = true;

   return false;
}

bool cmd_rxvol(int argc, char **args) {
   int val = atoi(args[1]) / 100;

   if (ui_mode == UI_MODE_TUI) {
      // do stuff
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
#endif
      ui_print(NULL, "* Set rx-vol to %f", val);
   }

   return false;
}

bool cmd_server(int argc, char **args) {
   // With no argument, show the server picker rather than silently
   // reconnecting to the current profile.
   if (argc < 2 || !args || !args[1] || args[1][0] == '\0') {
      show_server_chooser();

      return true;
   }

       const char *server = args[1];

       // Trim trailing whitespace; tab completion adds a space after the name
       char trimmed[64];
       snprintf(trimmed, sizeof(trimmed), "%s", server ? server : "");
       for (char *tp = trimmed + strlen(trimmed); tp > trimmed && isspace( (unsigned char)tp[-1] ); tp--) {
          tp[-1] = '\0';
       }
       server = trimmed;

       if (server && server[0] != '\0') {
         ui_print(NULL, "%s * Changing server profile to %s", get_chat_ts(now), server);
         disconnect_server(server);

         // Set the profile name unconditionally, server_name may be NULL on a
         // fresh start when nothing has connected yet
         free( (char *)server_name );
         server_name = strdup(server);

         if (!server_name) {
            fprintf(stderr, "OOM in parse_chat_input /server\n");

            return true;
         }
         Log(LOG_DEBUG, "gtk.core", "Set server profile to %s by console cmd", server);
         connect_server(server);
      } else {
         ui_print(NULL, "Try /server servername to connect");
         show_server_chooser();
      }

      return false;
   }
