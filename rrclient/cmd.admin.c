//
// rrclient/cmd.admin.c: Admin related commands
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
extern rrconn_t *ws_conn;

bool cmd_die(int argc, char **args) {
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "die");
   dict_add(d, "talk.args", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_kick(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      ui_print(NULL, "Usage: /kick <user> <reason>");
      return true;
   }

   // Everything after the target is the reason; the server requires a
   // minimum length (CHAT_MIN_REASON_LEN in librrprotocol/srv.chat.c)
   char reason[256] = "";

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(reason + strlen(reason), sizeof(reason) - strlen(reason), "%s%s",
         (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0) {
         break;
      }
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "kick");
   dict_add(d, "talk.target", args[1]);

   if (reason[0]) {
      dict_add(d, "talk.reason", reason);
   }
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

/* PARITY: rustyrig-www/js/webui (mute/unmute send msg.type talk, talk.cmd) */
bool cmd_mute(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      ui_print(NULL, "Usage: /mute <user> [reason]");
      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "mute");
   dict_add(d, "talk.target", args[1]);

   if (args[2]) {
      dict_add(d, "talk.reason", args[2]);
   }
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_unmute(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      ui_print(NULL, "Usage: /unmute <user>");
      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "unmute");
   dict_add(d, "talk.target", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_raw(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }
   char fullmsg[502];
   memset(fullmsg, 0, sizeof(fullmsg) );
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

// /syslog <on|off>: toggle the server's host log stream (FLAG_SYSLOG).
// The server reads the requested state from talk.target (see
// librrprotocol/srv.chat.c ws_chat_cmd_syslog); the log lines come back
// as RR_BINFRAME_SUBSYS_LOG binframes for the Host Log tab.
// PARITY: rustyrig-www/js/webui.chat.js (syslog toggle)
bool cmd_syslog(int argc, char **args) {
   if (argc < 2 || !args[1] || (!strcasecmp(args[1], "on") && !strcasecmp(args[1], "off") ) ) {
      ui_print(NULL, "Usage: /syslog <on|off>");
      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "syslog");
   dict_add(d, "talk.target", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   ui_print(NULL, "{yellow}Host log streaming %s (server permitting){reset}", args[1]);
   return false;
}

bool cmd_restart(int argc, char **args) {
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "restart");
   dict_add(d, "talk.reason", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

/* PARITY: rustyrig-www/js/webui (rehash sends msg.type:rehash) */
bool cmd_rehash(int argc, char **args) {
   (void)argc;
   (void)args;

   if (!ws_conn) {
      ui_print(NULL, "{red}Not connected to a server!{reset}");
      return true;
   }

   dict *d = dict_new();
   dict_add_ulong(d, "msg.ts", now);
   dict_add(d, "msg.type", "rehash");
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   ui_print(NULL, "Rehash requested from server");
   return false;
}

/* PARITY: rustyrig-www/js/webui.chat.js /quota (sends talk.cmd=quota) */
bool cmd_quota(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      // Bare /quota is a shortcut for LIST + showing the help
      ui_print(NULL, "{reset}Usage: /quota LIST | SHOW <user>... | ADD <user> <minutes> | RESET <user>... | SET <user> <minutes>");
      ui_print(NULL, "     ADD/SET take seconds or dhms string (ex: 1h30m) (SET 0 = no TX allowed)");

      // Send LIST to the server for the actual listing
      args[1] = (char *)"LIST";
      argc = 2;
   }

   dict *d = dict_new();

   if (!d) {
      ui_print(NULL, "{red}/quota: out of memory{reset}");
      return true;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "quota");
   dict_add(d, "talk.target", args[1]);

   // Extra users go into the data tail: "SHOW user2 user3..." etc.
   // The first user rides in talk.target like the webui does.
   char tail[512] = "";

   if (strcasecmp(args[1], "list") == 0 || strcasecmp(args[1], "show") == 0 ||
       strcasecmp(args[1], "add") == 0 || strcasecmp(args[1], "reset") == 0 ||
       strcasecmp(args[1], "set") == 0 || strcasecmp(args[1], "help") == 0) {
      size_t pos = 0;

      for (int i = 2 ; i < argc ; i++) {
         int n = snprintf(tail + pos, sizeof(tail) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

         if (n < 0 || (size_t)n >= sizeof(tail) - pos) {
            break;
         }
         pos += n;
      }
   } else {
      // Single-user shortcut: "/quota bob" == "SHOW bob". Leave data empty;
      // the server maps a non-keyword target with no data to SHOW target.
   }

   if (tail[0]) {
      dict_add(d, "talk.data", tail);
   }
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}
