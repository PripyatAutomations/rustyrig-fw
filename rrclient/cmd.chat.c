//
// rrclient/cmd.chat.c: Chat related stuff
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
extern bool rrclient_send_chat(const char *data);
extern bool syslog_clear(void);
extern const char *server_name; // remove this (connman.c)

#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
#endif

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

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", buf, VAL_STR, "talk.msg_type",
      "action");

#if defined(USE_MONGOOSE)

   if (ws_conn) {
      mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   }
#endif
   free( (void *)jp );

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

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", fullmsg, VAL_STR, "talk.target",
      target);
#if defined(USE_MONGOOSE)

   if (ws_conn) {
      mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   }
#endif
   free( (void *)jp );

   return false;
}

bool cmd_notice(int argc, char **args) {
   if (argc < 2) {
      // XXX: cry not enough args
      return true;
   }
#if     0
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

   // There's a window here at least...
   if (wp->cptr) {
      char *target = wp->title;

      if (args[1]) {
         target = args[1];
      }
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

      ui_print(wp, "-> *%s* %s", target, fullmsg);
      ui_print(wp, "{yellow}NOTICE is not supported over WebSocket{reset}");
   }
#endif

   return false;
}

bool cmd_part(int argc, char **args) {
   ui_print(NULL, "{yellow}PART is not supported over WebSocket{reset}");

   return false;
}

bool cmd_topic(int argc, char **args) {
   ui_print(NULL, "{yellow}TOPIC is not supported over WebSocket{reset}");

   return false;
}
