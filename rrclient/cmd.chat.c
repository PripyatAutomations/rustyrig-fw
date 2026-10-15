//
// rrclient/cmd.chat.c: Chat related stuff
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
#include <rrclient/userlist.h>
#include <rrclient/rooms.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;

/* Join the complete IRC text before the protocol layer splits wire lines. */
static bool cmd_irc_chat(rrconn_t *connection, const char *target, const char *kind, int argc, char **args, int first) {
   size_t length = 0;

   for (int i = first ; i < argc ; i++) {
      size_t part = args[i] ? strlen(args[i]) : 0;

      if (part >= SIZE_MAX - length - 1) {
         return false;
      }
      length += part + (i > first);
   }

   char *text = malloc(length + 1);
   dict *request = dict_new();

   if (!text || !request) {
      free(text);
      dict_free(request);

      return false;
   }
   char *out = text;

   for (int i = first ; i < argc ; i++) {
      if (i > first) {
         *out++ = ' ';
      }
      const char *part = args[i] ? args[i] : "";
      size_t size = strlen(part);
      memcpy(out, part, size);
      out += size;
   }

   *out = '\0';
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", "msg");
   dict_add(request, "talk.target", target);
   dict_add(request, "talk.msg_type", kind);
   dict_add(request, "talk.data", text);
   bool sent = irc_send_dict(connection, request);
   dict_free(request);
   free(text);

   return sent;
}

static bool cmd_irc_room(rrconn_t *connection, const char *room) {
   if (!connection || connection->is_ws || !room || (room[0] != '#' && room[0] != '&')) {
      ui_print(ui_active_window_name(), "Select an IRC channel tab first");

      return false;
   }

   return true;
}

/* PARITY: rustyrig-www/js/webui.chat.js room command forwarding.
 * /room is deliberately sent to the server.  Room administration belongs to the server database and the server decides whether this user may perform the
 * requested operation.
 */
bool cmd_room(int argc, char **args) {
   if (!ws_conn) {
      return true;
   }
   char data[512] = "";

   for (int i = 1 ; i < argc ; i++) {
      if (i > 1) {
         strlcat(data, " ", sizeof(data));
      }
      strlcat(data, args[i], sizeof(data));
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "room");
   dict_add(d, "talk.data", data);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

/* PARITY: rustyrig-www/js/webui.chat.js /users forwarding. */
bool cmd_users(int argc, char **args) {
   (void)argc;
   (void)args;
   rrconn_t *connection = rrclient_active_connection();

   if (!connection) {
      return true;
   }

   if (!connection->is_ws) {
      ui_print(ui_active_window_name(), "/users requires a RustyRig connection; use /names on IRC");

      return false;
   }
   dict *d = dict_new();

   if (!d) {
      return true;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "users");
   const char *room = rrclient_active_room();

   if (room) {
      dict_add(d, "talk.target", room);
   }
   bool sent = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return !sent;
}

bool cmd_list(int argc, char **args) {
   (void)argc;
   (void)args;
   rrconn_t *connection = rrclient_active_connection();

   if (connection && !connection->is_ws) {
      bool sent = irc_send(connection, "LIST");

      return !sent;
   }

   if (!connection) {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "list");
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_query(int argc, char **args) {
   if (argc < 2 || !args[1] || !*args[1]) {
      return true;
   }

   for (const unsigned char *p = (const unsigned char *)args[1] ; *p ; p++) {
      if (*p <= ' ' || *p == 127 || (p == (const unsigned char *)args[1] && *p == ':')) {
         ui_print(ui_active_window_name(), "Usage: /query user [message]");

         return true;
      }
   }

   /* PARITY: rustyrig-www/js/webui.chat.js:parse_chat_cmd query */
   rrconn_t *connection = rrclient_active_connection();

   if (ui_mode == UI_MODE_TUI) {
      const char *window_name = rrclient_window_name(args[1]);
      tui_window_t *window = tui_window_find(window_name);

      if (!window) {
         window = tui_window_create(window_name);
      }

      if (window) {
         window->cptr = connection;
         tui_window_focus(window->title);
      }
   } else if (frontend_ops()) {
      frontend_ops()->chat_query_add(args[1]);
   }

   return argc > 2 ? cmd_msg(argc, args) : false;
}

bool cmd_join(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (argc < 2 || !connection) {
      return true;
   }

   if (!connection->is_ws) {
      if (!args[1] || !*args[1] || strpbrk(args[1], " \t\r\n,")) {
         ui_print(ui_active_window_name(), "Usage: /join <IRC channel>");

         return false;
      }
      bool sent = irc_send(connection, "JOIN %s", args[1]);
      const char *window_name = rrclient_window_name(args[1]);

      if (sent && ui_mode == UI_MODE_TUI) {
         tui_window_t *window = tui_window_find(window_name);

         if (!window) {
            window = tui_window_create(window_name);
         }

         if (window) {
            window->cptr = connection;
            tui_window_focus(window->title);
         }
      } else if (sent && ui_mode == UI_MODE_GTK && frontend_ops() && frontend_ops()->chat_room_add) {
         rrclient_connection_select(rrclient_window_server(window_name));
         frontend_ops()->chat_room_add(args[1]);
      }

      return !sent;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "join");
   dict_add(d, "talk.target", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool cmd_me(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();
   const char *active_room = rrclient_active_room();

   if (connection && !connection->is_ws) {
      if (!cmd_irc_room(connection, active_room) || argc < 2) {
         return false;
      }

      return !cmd_irc_chat(connection, active_room, "action", argc, args, 1);
   }
   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;

   for (int i = 1 ; i < argc ; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.data", buf);
   dict_add(d, "talk.msg_type", "action");

   if (frontend_ops()) {
      const char *room = frontend_ops()->chat_current_room();

      if (room && room[0]) {
         dict_add(d, "talk.target", room);
      }
   }

   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_active_window();

      if (window && window->title[0] && strcasecmp(window->title, "status") != 0) {
         dict_add(d, "talk.target", rrclient_window_room(window->title));
      } else if (cfg_get_bool("tui.status-chat", false)) {
         const char *room = ws_authoritative_room();

         if (room && *room) {
            dict_add(d, "talk.target", room);
         }
      } else {
         ui_print(ui_active_window_name(), "\00308Select a room tab before sending an action (status is for client logs and commands)\017");
         dict_free(d);

         return false;
      }
   }

   if (ws_conn) {
      ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   }
   dict_free(d);

   return false;
}

bool cmd_msg(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (argc < 3 || !args[1] || !args[2] || !*args[1]) {
      return true;
   }

   char *target = args[1];

   if (connection && !connection->is_ws) {
      bool sent = cmd_irc_chat(connection, target, target[0] == '#' || target[0] == '&' ? "pub" : "priv", argc, args, 2);

      if (sent && ui_mode == UI_MODE_TUI) {
         const char *window_name = rrclient_window_name(target);
         tui_window_t *window = tui_window_find(window_name);

         if (!window) {
            window = tui_window_create(window_name);
         }

         if (window) {
            window->cptr = connection;
         }
      }

      return !sent;
   }

   char fullmsg[502];
   memset(fullmsg, 0, sizeof(fullmsg));
   size_t pos = 0;

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }

   if (ui_mode == UI_MODE_TUI) {
      const char *window_name = rrclient_window_name(target);
      tui_window_t *window = tui_window_find(window_name);

      if (!window) {
         window = tui_window_create(window_name);
      }

      if (window) {
         window->cptr = connection;
      }
   } else if (frontend_ops()) {
      frontend_ops()->chat_query_add(target);
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.data", fullmsg);
   dict_add(d, "talk.target", target);
   dict_add(d, "talk.msg_type", "priv");

   if (ws_conn) {
      ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   }

   dict_free(d);

   return false;
}

bool cmd_notice(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();

   if (argc < 2) {
      return true;
   }

   char *notice_target = args[1];

   if (connection && !connection->is_ws) {
      if (argc < 3) {
         ui_print(ui_active_window_name(), "Usage: /notice <nick|#channel> <message>");

         return false;
      }

      return !cmd_irc_chat(connection, notice_target, "notice", argc, args, 2);
   }
   char notice_msg[502];
   memset(notice_msg, 0, sizeof(notice_msg));
   size_t pos = 0;

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(notice_msg + pos, sizeof(notice_msg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(notice_msg) - pos) {
         break;
      }
      pos += n;
   }

   if (ui_mode == UI_MODE_TUI) {
      const char *window_name = rrclient_window_name(args[1]);
      tui_window_t *window = tui_window_find(window_name);

      if (!window) {
         window = tui_window_create(window_name);
      }

      if (window) {
         window->cptr = connection;
      }
   } else if (frontend_ops()) {
      frontend_ops()->chat_query_add(args[1]);
   }

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.msg_type", "notice");
   dict_add(d, "talk.data", notice_msg);
   dict_add(d, "talk.target", notice_target);

   if (connection && connection->is_ws) {
      ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   }
   dict_free(d);

   return false;
}

bool cmd_part(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();
   const char *active_room = rrclient_active_room();

   if (!connection) {
      return true;
   }
   const char *target = argc >= 2 ? args[1] : active_room;
   const bool is_irc = !connection->is_ws;

   if (!target || !*target) {
      if (ui_mode == UI_MODE_TUI) {
         tui_window_t *window = tui_active_window();

         if (window && window->title[0] && strcasecmp(window->title, "status") != 0) {
            target = rrclient_window_room(window->title);
         }
      } else if (frontend_ops()) {
         target = frontend_ops()->chat_current_room();
      }
   }

   /* Query tabs are local conversations rather than joined server rooms. */
   if (!is_irc && argc < 2 && target && *target && target[0] != '#' && target[0] != '&') {
      if (frontend_ops()) {
         frontend_ops()->chat_room_remove(target);
      }

      if (ui_mode == UI_MODE_TUI) {
         tui_window_t *window = tui_window_find(rrclient_window_name(target));

         if (window) {
            tui_window_destroy(window);
         }
         tui_window_focus("status");
      }

      return false;
   }

   if (!target || !*target || (target[0] != '#' && target[0] != '&')) {
      ui_print(ui_active_window_name(), "\00308Usage: /part #room (select a room tab or provide the room)\017");

      return false;
   }

   if (is_irc) {
      const char *reason = argc >= 3 ? args[2] : NULL;

      if (strpbrk(target, " \t\r\n,")) {
         ui_print(ui_active_window_name(), "Invalid IRC channel name");

         return false;
      }

      if (reason && strpbrk(reason, "\r\n")) {
         ui_print(ui_active_window_name(), "Invalid IRC PART reason");

         return false;
      }
      bool sent = reason && *reason ? irc_send(connection, "PART %s :%s", target, reason) : irc_send(connection, "PART %s", target);

      if (!sent) {
         ui_print(ui_active_window_name(), "Unable to send IRC PART for %s", target);
      }

      return !sent;
   }
   dict *d = dict_new();

   if (!d) {
      return true;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "part");
   dict_add(d, "talk.target", target);
   bool sent = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   if (!sent) {
      ui_print(ui_active_window_name(), "Unable to send PART for %s", target);
   }

   return !sent;
}

// PARITY: rrclient/cmd.names.c (C client) - /names prints the userlist like an
// IRC client, with privilege flags and PTT state.
bool cmd_names(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();
   const char *room = rrclient_active_room();

   if (!connection) {
      return true;
   }

   if (!connection->is_ws) {
      bool sent = room && *room ? irc_send(connection, "NAMES %s", room) : irc_send(connection, "NAMES");

      return !sent;
   }

   if (!global_userlist) {
      ui_print(ui_active_window_name(), "\00308No users online\017");

      return true;
   }

   ui_print(ui_active_window_name(), "\00308Users online:\017");

   int count = 0;

   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (c->room[0] && room && strcasecmp(c->room, room) != 0) {
         continue;
      }
      count++;

      // @ before the name for admin|owner, + for noob.
      // A microphone after the name marks whoever holds the PTT.
      char prefix[8] = "";
      char suffix[16] = "";

      if (strcasestr(c->privs, "owner") || strcasestr(c->privs, "admin")) {
         strlcpy(prefix, "@", sizeof(prefix));
      } else if (strcasestr(c->privs, "noob")) {
         strlcpy(prefix, "+", sizeof(prefix));
      }

      if (c->is_ptt) {
         strlcpy(suffix, " 🎤", sizeof(suffix));
      }

      ui_print(ui_active_window_name(), "  \00300%s%s%s\017", prefix, c->name, suffix);
   }

   ui_print(ui_active_window_name(), "\00308%d user%s online\017", count, (count == 1) ? "" : "s");

   return true;
}

bool cmd_topic(int argc, char **args) {
   rrconn_t *connection = rrclient_active_connection();
   const char *room = rrclient_active_room();

   if (connection && !connection->is_ws) {
      if (!cmd_irc_room(connection, room)) {
         return false;
      }

      if (argc < 2) {
         bool sent = irc_send(connection, "TOPIC %s", room);

         return !sent;
      }
      char topic[IRC_MSGLEN] = "";

      for (int i = 1 ; i < argc ; i++) {
         if ((i > 1 && strlcat(topic, " ", sizeof(topic)) >= sizeof(topic)) || strlcat(topic, args[i], sizeof(topic)) >= sizeof(topic)) {
            ui_print(ui_active_window_name(), "IRC topic is too long");

            return false;
         }
      }

      bool sent = irc_send(connection, "TOPIC %s :%s", room, topic);

      return !sent;
   }

   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_active_window();

      if (window && window->title[0] && strcasecmp(window->title, "status") != 0) {
         room = rrclient_window_room(window->title);
      }
   } else if (frontend_ops()) {
      room = frontend_ops()->chat_current_room();
   }

   if (!room || (room[0] != '#' && room[0] != '&')) {
      ui_print(ui_active_window_name(), "\00308Select a room tab before using /topic\017");

      return false;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "topic");
   dict_add(d, "talk.target", room);

   if (argc > 1) {
      char topic[512] = "";

      for (int i = 1 ; i < argc ; i++) {
         if (i > 1) {
            strlcat(topic, " ", sizeof(topic));
         }
         strlcat(topic, args[i], sizeof(topic));
      }

      dict_add(d, "talk.data", topic);
   } else {
      dict_add(d, "talk.data", "");
   }

   if (ws_conn) {
      ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   }
   dict_free(d);

   return false;
}
