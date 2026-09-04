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
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <ev.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;

bool cmd_join(int argc, char **args) {
   ui_print(NULL, "{yellow}JOIN is not supported over WebSocket{reset}");

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

   if (ws_conn) {
      ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   }
   dict_free(d);

   return false;
}

bool cmd_msg(int argc, char **args) {
   if (argc < 2) {
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
   ui_print(NULL, "-> %s %s", target, fullmsg);

   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.data", fullmsg);
   dict_add(d, "talk.target", target);

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

         ui_print(NULL, "-> *%s* %s", notice_target, notice_msg);
      }
   }
   ui_print(NULL, "TEST: {yellow}=> *%s*{reset} {bright-cyan}%s{reset}: %s", notice_target, notice_msg);

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
   ui_print(NULL, "{yellow}PART is not supported over WebSocket yet{reset}");

   return false;
}

bool cmd_topic(int argc, char **args) {
   ui_print(NULL, "{yellow}TOPIC is not supported over WebSocket yet{reset}");

   return false;
}
