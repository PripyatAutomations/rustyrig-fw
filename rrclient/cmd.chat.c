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
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/rooms.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#ifdef USE_GTK
#include <rrclient/gtk.chat.h>
#endif

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;

/* /room is deliberately sent to the server.  Room administration belongs to
 * the server database and the server decides whether this user may perform
 * the requested operation. */
bool cmd_room(int argc, char **args) {
   if (!ws_conn) return true;
   char data[512] = "";
   for (int i = 1; i < argc; i++) {
      if (i > 1) strlcat(data, " ", sizeof(data));
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

bool cmd_list(int argc, char **args) {
   (void)argc; (void)args;
   if (!ws_conn) return true;
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "list");
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return false;
}

bool cmd_query(int argc, char **args) {
   if (argc < 2 || !args[1] || !*args[1]) return true;
   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_window_find(args[1]);
      if (!window) window = tui_window_create(args[1]);
      if (window) {
         window->cptr = ws_conn;
         tui_window_focus(window->title);
      }
   }
#ifdef USE_GTK
   else if (ui_mode == UI_MODE_GTK) {
      gtk_chat_room_add(args[1]);
   }
#endif
   return false;
}

bool cmd_join(int argc, char **args) {
   if (argc < 2 || !ws_conn) {
      return true;
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
#ifdef USE_GTK
   if (ui_mode == UI_MODE_GTK) {
      const char *room = gtk_chat_current_room();
      if (room && room[0]) {
         dict_add(d, "talk.target", room);
      }
   }
#endif
   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_active_window();
      if (window && window->title[0] && strcasecmp(window->title, "status") != 0) {
         dict_add(d, "talk.target", window->title);
      } else if (cfg_get_bool("tui.status-chat", false)) {
         const char *room = ws_authoritative_room();
         if (room && *room) dict_add(d, "talk.target", room);
      } else {
         ui_print(ui_active_window_name(), "{yellow}Select a room tab before sending an action (status is for client logs and commands){reset}");
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
   if (argc < 3 || !args[1] || !args[2] || !*args[1]) {
      return true;
   }

   char *target = args[1];
   char fullmsg[502];
   memset( fullmsg, 0, sizeof(fullmsg) );
   size_t pos = 0;

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }
   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_window_find(target);
      if (!window) {
         window = tui_window_create(target);
      }
      if (window) {
         window->cptr = ws_conn;
         ui_print(target, "-> %s %s", target, fullmsg);
      } else {
         ui_print(ui_active_window_name(), "-> %s %s", target, fullmsg);
      }
   }
#ifdef USE_GTK
   else if (ui_mode == UI_MODE_GTK) {
      gtk_chat_room_add(target);
      ui_print(target, "-> %s %s", target, fullmsg);
   }
#endif
   else {
      ui_print(ui_active_window_name(), "-> %s %s", target, fullmsg);
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
   if (argc < 2) {
      return true;
   }

   char *notice_target = args[1];
   char notice_msg[502];
   memset( notice_msg, 0, sizeof(notice_msg) );
   size_t pos = 0;

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(notice_msg + pos, sizeof(notice_msg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(notice_msg) - pos) {
         break;
      }
      pos += n;
   }

   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *wp = NULL;
      bool new_win = false;

      if (*args[1]) {
         wp = tui_window_find(args[1]);

         if (!wp) {
            new_win = true;
            wp = tui_window_create(args[1]);
            wp->cptr = tui_active_window()->cptr;
         }
      }

      if (!wp) {
         wp = tui_active_window();
      }

      if (wp->cptr) {
         char *notice_target = wp->title;

         ui_print(ui_active_window_name(), "-> *%s* %s", notice_target, notice_msg);
      }
   }
   ui_print(ui_active_window_name(), "TEST: {yellow}=> *%s*{reset} {bright-cyan}%s{reset}: %s", notice_target, notice_msg);

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.msg-type", (char *)"notice");
   dict_add(d, "talk.msg", notice_msg);
   dict_add(d, "talk.target", notice_target);

   // XXX: Send it to the network
   // Send it for display? (XXX: Should we do this or let the network echo it back?)
   event_emit_dict("talk", NULL, d);
   dict_free(d);
   return false;
}

bool cmd_part(int argc, char **args) {
   if (!ws_conn) {
      return true;
   }
   const char *target = argc >= 2 ? args[1] : NULL;
   if (!target || !*target) {
      if (ui_mode == UI_MODE_TUI) {
         tui_window_t *window = tui_active_window();
         if (window && window->title[0] &&
             strcasecmp(window->title, "status") != 0)
            target = window->title;
      }
#ifdef USE_GTK
      else if (ui_mode == UI_MODE_GTK) {
         target = gtk_chat_current_room();
      }
#endif
   }
   /* Query tabs are local conversations rather than joined server rooms. */
   if (argc < 2 && target && *target && target[0] != '#' && target[0] != '&') {
#ifdef USE_GTK
      if (ui_mode == UI_MODE_GTK) gtk_chat_room_remove(target);
#endif
      if (ui_mode == UI_MODE_TUI) {
         tui_window_t *window = tui_window_find(target);
         if (window) tui_window_destroy(window);
         tui_window_focus("status");
      }
      return false;
   }
   if (!target || !*target || (target[0] != '#' && target[0] != '&')) {
      ui_print(ui_active_window_name(), "{yellow}Usage: /part #room (select a room tab or provide the room){reset}");
      return false;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "part");
   dict_add(d, "talk.target", target);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

// PARITY: rrclient/cmd.names.c (C client) - /names prints the userlist like an
// IRC client, with privilege flags and PTT state.
bool cmd_names(int argc, char **args) {
   if (!global_userlist) {
      ui_print(ui_active_window_name(), "{yellow}No users online{reset}");
      return true;
   }

   ui_print(ui_active_window_name(), "{yellow}Users online:{reset}");

   int count = 0;
   for (struct rr_user *c = global_userlist; c; c = c->next) {
      if (c->room[0] && strcasecmp(c->room, rrclient_current_room()) != 0) continue;
      count++;

      // @ before the name for admin|owner, + for noob.
      // A microphone after the name marks whoever holds the PTT.
      char prefix[8] = "";
      char suffix[16] = "";

      if ( strcasestr(c->privs, "owner") || strcasestr(c->privs, "admin") ) {
         strlcpy(prefix, "@", sizeof(prefix));
      } else if ( strcasestr(c->privs, "noob") ) {
         strlcpy(prefix, "+", sizeof(prefix));
      }

      if (c->is_ptt) {
         strlcpy(suffix, " 🎤", sizeof(suffix));
      }

      ui_print(ui_active_window_name(), "  {white}%s%s%s{reset}", prefix, c->name, suffix);
   }

   ui_print(ui_active_window_name(), "{yellow}%d user%s online{reset}", count, (count == 1) ? "" : "s");
   return true;
}

bool cmd_topic(int argc, char **args) {
   const char *room = NULL;
   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_active_window();
      if (window && window->title[0] && strcasecmp(window->title, "status") != 0)
         room = window->title;
   }
#ifdef USE_GTK
   else if (ui_mode == UI_MODE_GTK) {
      room = gtk_chat_current_room();
   }
#endif
   if (!room || (room[0] != '#' && room[0] != '&')) {
      ui_print(ui_active_window_name(), "{yellow}Select a room tab before using /topic{reset}");
      return false;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "topic");
   dict_add(d, "talk.target", room);
   if (argc > 1) {
      char topic[512] = "";
      for (int i = 1; i < argc; i++) {
         if (i > 1) strlcat(topic, " ", sizeof(topic));
         strlcat(topic, args[i], sizeof(topic));
      }
      dict_add(d, "talk.data", topic);
   } else {
      dict_add(d, "talk.data", "");
   }
   if (ws_conn) ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return false;
}
