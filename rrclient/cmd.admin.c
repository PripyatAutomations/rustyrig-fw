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
#include <librrprotocol/irc.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;

// /die <reason>: shutdown the server. Server requires a reason of at least
// CHAT_MIN_REASON_LEN chars and reads it at talk.args.reason; msg.type must
// be "talk" or the server drops the message as unset msg_type.
// PARITY: www/js/webui.chat.js chat_send_command() (talk.args.reason)
bool cmd_die(int argc, char **args) {
   if (argc < 2 || !args[1] || strlen(args[1]) < 10) {
      ui_print(ui_active_window_name(), "Usage: /die <reason> (min 10 chars)");

      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "die");
   dict_add(d, "talk.args.reason", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_kick(int argc, char **args) {
   return rrclient_kick_send(rrclient_active_connection(), rrclient_active_room(), ui_active_window_name(), argc, args);
}

bool rrclient_kick_send(rrconn_t *connection, const char *room, const char *window, int argc, char **args) {
   if (argc < 2 || !args || !args[1] || !connection) {
      ui_print(window, "Usage: /kick <user> [reason]");

      return true;
   }

   if (!connection->is_ws && (!connection->connected || !connection->authenticated)) {
      ui_print(window, "Not connected to IRC server");

      return false;
   }

   if (!connection->is_ws) {
      if (!args[1] || !*args[1] || strpbrk(args[1], " \t\r\n,:")) {
         ui_print(window, "Invalid IRC nickname");

         return false;
      }

      if (!room || (room[0] != '#' && room[0] != '&')) {
         ui_print(window, "Select an IRC channel tab first");

         return false;
      }
      char reason[IRC_MSGLEN] = "";

      for (int i = 2 ; i < argc ; i++) {
         if ((i > 2 && strlcat(reason, " ", sizeof(reason)) >= sizeof(reason)) || strlcat(reason, args[i], sizeof(reason)) >= sizeof(reason)) {
            ui_print(window, "IRC kick reason is too long");

            return false;
         }
      }

      if (strpbrk(room, " \t\r\n,")) {
         ui_print(window, "Invalid IRC channel name");

         return false;
      }
      bool sent = reason[0] ? irc_send(connection, "KICK %s %s :%s", room, args[1], reason) : irc_send(connection, "KICK %s %s", room, args[1]);

      if (!sent) {
         ui_print(window, "Unable to send IRC KICK for %s", args[1]);
      }

      return !sent;
   }
   // Everything after the target is the reason; the server requires a
   // minimum length (CHAT_MIN_REASON_LEN in librrprotocol/srv.chat.c)
   char reason[256] = "";

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(reason + strlen(reason), sizeof(reason) - strlen(reason), "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0) {
         break;
      }
   }

   dict *d = dict_new();

   if (!d) {
      return true;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "kick");
   dict_add(d, "talk.target", args[1]);

   if (reason[0]) {
      dict_add(d, "talk.reason", reason);
   }
   bool sent = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return !sent;
}

/* PARITY: rustyrig-www/js/webui (mute/unmute send msg.type talk, talk.cmd) */
bool cmd_mute(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (!connection || !connection->is_ws) {
      ui_print(ui_active_window_name(), "/mute is only available on RustyRig connections");

      return false;
   }

   if (argc < 2 || !args[1]) {
      ui_print(ui_active_window_name(), "Usage: /mute <user> [reason]");

      return true;
   }

   if (!ws_conn) {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "mute");
   dict_add(d, "talk.target", args[1]);

   if (argc > 2 && args[2]) {
      dict_add(d, "talk.reason", args[2]);
   }
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_unmute(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (!connection || !connection->is_ws) {
      ui_print(ui_active_window_name(), "/unmute is only available on RustyRig connections");

      return false;
   }

   if (argc < 2 || !args[1]) {
      ui_print(ui_active_window_name(), "Usage: /unmute <user>");

      return true;
   }

   if (!ws_conn) {
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

bool cmd_nick(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (!connection || connection->is_ws) {
      ui_print(ui_active_window_name(), "/nick is only available on IRC connections");

      return false;
   }

   if (!connection->connected) {
      ui_print(ui_active_window_name(), "Not connected to IRC server");

      return false;
   }

   if (argc < 2 || !args[1] || !*args[1] || strlen(args[1]) > NICKLEN || strpbrk(args[1], " ,*?!@:")) {
      ui_print(ui_active_window_name(), "Usage: /nick <nickname>");

      return false;
   }
   bool sent = rrclient_irc_nick_submit(rrclient_connection_name(connection), args[1]);

   if (!sent) {
      ui_print(ui_active_window_name(), "Unable to send IRC NICK");
   }

   return !sent;
}

bool cmd_mode(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();
   const char *room = rrclient_active_room();

   if (!connection || connection->is_ws) {
      ui_print(ui_active_window_name(), "/mode is only available on IRC connections");

      return false;
   }
   int first_mode = 1;

   if (argc > 2 && (args[1][0] == '#' || args[1][0] == '&')) {
      room = args[1];
      first_mode = 2;
   }

   if (!connection->connected) {
      ui_print(ui_active_window_name(), "Not connected to IRC server");

      return false;
   }

   if (!room || (room[0] != '#' && room[0] != '&') || argc <= first_mode || !args[first_mode][0] || strpbrk(room, " \t\r\n,")) {
      ui_print(ui_active_window_name(), "Usage: /mode [+|-mode] [arguments] (select an IRC channel tab)");

      return false;
   }
   char command[IRC_MSGLEN] = "";
   int length = snprintf(command, sizeof(command), "MODE %s", room);

   for (int i = first_mode ; i < argc ; i++) {
      if (length < 0 || (size_t)length >= sizeof(command)) {
         return false;
      }
      int added = snprintf(command + length, sizeof(command) - (size_t)length, " %s", args[i]);

      if (added < 0 || (size_t)added >= sizeof(command) - (size_t)length) {
         ui_print(ui_active_window_name(), "IRC MODE command is too long");

         return false;
      }
      length += added;
   }

   bool sent = irc_send(connection, "%s", command);

   if (!sent) {
      ui_print(ui_active_window_name(), "Unable to send IRC MODE");
   }

   return !sent;
}

bool cmd_ban(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();
   const char *room = rrclient_active_room();

   if (!connection || connection->is_ws) {
      ui_print(ui_active_window_name(), "/ban is only available on IRC connections");

      return false;
   }

   if (!connection->connected) {
      ui_print(ui_active_window_name(), "Not connected to IRC server");

      return false;
   }

   if (argc < 2 || !args[1] || !*args[1] || !room || (room[0] != '#' && room[0] != '&') || strpbrk(args[1], " \t\r\n,")) {
      ui_print(ui_active_window_name(), "Usage: /ban <nick|mask> (select an IRC channel tab)");

      return false;
   }
   bool sent = irc_send(connection, "MODE %s +b %s", room, args[1]);

   if (!sent) {
      ui_print(ui_active_window_name(), "Unable to send IRC BAN");
   }

   return !sent;
}

bool cmd_raw(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (!connection) {
      ui_print(ui_active_window_name(), "No active connection");

      return true;
   }

   if (!connection->is_ws && argc < 2) {
      ui_print(ui_active_window_name(), "Usage: /raw <IRC command> [arguments]");

      return true;
   }

   if (!connection->is_ws) {
      char command[IRC_MSGLEN] = "";

      for (int i = 1 ; i < argc ; i++) {
         if ((i > 1 && strlcat(command, " ", sizeof(command)) >= sizeof(command)) || strlcat(command, args[i], sizeof(command)) >= sizeof(command)) {
            ui_print(ui_active_window_name(), "IRC command is too long");

            return false;
         }
      }

      return !irc_send(connection, "%s", command);
   }
   char fullmsg[502];
   memset(fullmsg, 0, sizeof(fullmsg));
   size_t pos = 0;

   for (int i = 1 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }

   ui_print(ui_active_window_name(), "-raw-> %s", fullmsg);
   ui_print(ui_active_window_name(), "\00308QUOTE is not supported over WebSocket\017");

   return false;
}

// /syslog <on|off>: toggle the server's host log stream (FLAG_SYSLOG).
// The server reads the requested state from talk.target (see
// librrprotocol/srv.chat.c ws_chat_cmd_syslog); the log lines come back
// as RR_BINFRAME_SUBSYS_LOG binframes for the Host Log tab.
// PARITY: rustyrig-www/js/webui.chat.js (syslog toggle)
bool cmd_syslog(int argc, char **args) {
   if (argc < 2 || !args[1] || (!strcasecmp(args[1], "on") && !strcasecmp(args[1], "off"))) {
      ui_print(ui_active_window_name(), "Usage: /syslog <on|off>");

      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "syslog");
   dict_add(d, "talk.target", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   ui_print(ui_active_window_name(), "\00308Host log streaming %s (server permitting)\017", args[1]);

   return false;
}

// /restart <reason>: restart the server. Server requires a reason of at
// least CHAT_MIN_REASON_LEN chars and reads it at talk.args.reason; msg.type
// must be "talk" or the server drops the message as unset msg_type.
// PARITY: www/js/webui.chat.js chat_send_command() (talk.args.reason)
bool cmd_restart(int argc, char **args) {
   if (argc < 2 || !args[1] || strlen(args[1]) < 10) {
      ui_print(ui_active_window_name(), "Usage: /restart <reason> (min 10 chars)");

      return true;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "restart");
   dict_add(d, "talk.args.reason", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

/* PARITY: rustyrig-www/js/webui (rehash sends msg.type:rehash) */
bool cmd_rehash(int argc, char **args) {
   (void)argc;
   (void)args;

   if (!ws_conn) {
      ui_print(ui_active_window_name(), "\00304Not connected to a server!\017");

      return true;
   }

   dict *d = dict_new();
   dict_add_ulong(d, "msg.ts", now);
   dict_add(d, "msg.type", "rehash");
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   ui_print(ui_active_window_name(), "Rehash requested from server");

   return false;
}

/* PARITY: rustyrig-www/js/webui.chat.js /quota (sends talk.cmd=quota) */
bool cmd_quota(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      // Send LIST to the server for the actual listing
      args[1] = (char *)"LIST";
      argc = 2;
   }

   dict *d = dict_new();

   if (!d) {
      ui_print(ui_active_window_name(), "\00304/quota: out of memory\017");

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
      strcasecmp(args[1], "set") == 0 || strcasecmp(args[1], "help") == 0 || !strcasecmp(args[1], "BW") || !strcasecmp(args[1], "TX")) {
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

/* PARITY: librrprotocol/srv.chat.c /user and rrserver/events.c */
bool cmd_user(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      ui_print(ui_active_window_name(),
         "Usage: /user list | add <user> [privileges] | remove <user> | lock <user> | unlock <user> | privs <user> list|add|remove|set [privileges] | "
         "oldpw | resetpw <user> | pass <user> <password>");

      return true;
   }
   dict *d = dict_new();

   if (!d) {
      ui_print(ui_active_window_name(), "\00304/user: out of memory\017");

      return true;
   }
   char data[512] = "";
   size_t pos = 0;

   for (int i = 1 ; i < argc ; i++) {
      int n = snprintf(data + pos, sizeof(data) - pos, "%s%s", i > 1 ? " " : "", args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(data) - pos) {
         break;
      }
      pos += (size_t)n;
   }

   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "user");
   dict_add(d, "talk.data", data);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

/* PARITY: rustyrig-www/js/webui.chat.js link/admin command tails. */
static bool send_management_command(const char *command, int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (!connection || !connection->is_ws) {
      ui_print(ui_active_window_name(), "/%s requires a RustyRig connection", command);

      return true;
   }
   char tail[512] = "";

   for (int i = 1 ; i < argc ; i++) {
      if ((i > 1 && strlcat(tail, " ", sizeof(tail)) >= sizeof(tail)) || strlcat(tail, args[i], sizeof(tail)) >= sizeof(tail)) {
         return true;
      }
   }

   dict *request = dict_new();

   if (!request) {
      return true;
   }
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", command);
   dict_add(request, "talk.data", tail);
   bool sent = ws_send_dict(NULL, connection, request, WEBSOCKET_OP_TEXT);
   dict_free(request);

   return !sent;
}

bool cmd_link(int argc, char **args) {
   return send_management_command("link", argc, args);
}

bool cmd_admin_query(int argc, char **args) {
   return send_management_command("admin", argc, args);
}

bool cmd_supervise(int argc, char **args) {
   rrconn_t *client = rrclient_active_connection();

   if (!client || !client->is_ws || argc != 4) {
      ui_print(ui_active_window_name(), "Usage: /supervise accept|decline <request-id> <rig-room> (RustyRig only)");

      return true;
   }
   dict *d = dict_new();

   if (!d) {
      return true;
   }
   char tail[96];
   snprintf(tail, sizeof(tail), "%s %s", args[1], args[2]);
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "supervise");
   dict_add(d, "talk.room", args[3]);
   dict_add(d, "talk.data", tail);
   bool sent = ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return !sent;
}
