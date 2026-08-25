//
// rrclient/cmd.admin.c: Admin related commands
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
extern rrconn_t *ws_conn;

bool cmd_die(int argc, char **args) {
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "die");
   dict_add(d, "talk.args", args[1]);
#ifdef USE_MONGOOSE
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
#endif // USE_MONGOOSE
   dict_free(d);

   return false;
}

bool cmd_kick(int argc, char **args) {
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "kick");
   dict_add(d, "talk.reason", args[1]);
#ifdef USE_MONGOOSE
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
#endif // USE_MONGOOSE
   dict_free(d);

   return false;
}

bool cmd_quote(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }
   char fullmsg[502];
   memset( fullmsg, 0, sizeof(fullmsg) );
   size_t pos = 0;

   for (int i = 1 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }

   ui_print(NULL, "-raw-> %s", fullmsg);
   ui_print(NULL, "{yellow}QUOTE is not supported over WebSocket{reset}");

   return false;
}

bool cmd_restart(int argc, char **args) {
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "restart");
   dict_add(d, "talk.reason", args[1]);
#ifdef USE_MONGOOSE
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
#endif // USE_MONGOOSE
   dict_free(d);

   return false;
}
