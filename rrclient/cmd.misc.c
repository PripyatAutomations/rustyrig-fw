//
// rrclient/cmd.misc.c: unsorted commands
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
extern bool ws_connected;
extern bool rrclient_send_chat(const char *data);
extern bool syslog_clear(void);
extern const char *server_name;	// remove this (connman.c)

#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
#endif
bool cmd_clear(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      tui_clear_scrollback( tui_active_window() );
#if     defined(USE_GTK)
   } else {
      gtk_text_buffer_set_text(text_buffer, "", -1);
#endif
   }

   return false;
}

bool cmd_clearlog(int argc, char **args) {
   syslog_clear();
   return false;
}

bool cmd_disconnect(int argc, char **args) {
   disconnect_server(server_name);
   return false;
}

bool cmd_quit(int argc, char **args) {
   ui_print(NULL, "Goodbye!");

   const char *jp = dict2json_mkstr(VAL_STR, "auth.cmd", "quit", VAL_STR, "auth.msg", args[1]);
#if     defined(USE_MONGOOSE)
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif
   free( (char *)jp );

   // Set the dying flag so main loop with cleanly exit
   dying = true;

   return false;
}

bool cmd_rxvol(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      // do stuff
#if     defined(USE_GTK)
   } else if (ui_mode == UI_MODE_GTK) {
      gdouble val = atoi(args[1]) / 100;
      gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
      ui_print(NULL, "* Set rx-vol to %f", val);
#endif
   }
   return false;
}

bool cmd_server(int argc, char **args) {
   const char *server = args[1];

   if (server && strlen(server) > 1) {
      ui_print(NULL, "%s * Changing server profile to %s", get_chat_ts(now), server);
      disconnect_server(server);

      if (server_name) {
         free( (char *)server_name );
         server_name = strdup(server);

         if (!server_name) {
            fprintf(stderr, "OOM in parse_chat_input /server\n");

            return true;
         }
      }
      Log(LOG_DEBUG, "gtk.core", "Set server profile to %s by console cmd", server);
      connect_server(server);
   } else {
      ui_print(NULL, "Try /server servername to connect");
      show_server_chooser();
   }

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "restart", VAL_STR, "talk.reason", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );

   return false;
}
