//
// rrclient/chat.whois.c: Chat related stuff
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

extern dict *cfg;

bool cmd_whois(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      ui_print(NULL, "Usage: /whois <user>");
      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "whois");
   dict_add(d, "talk.target", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}
