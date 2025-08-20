
// gtk-client/ws.chat.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
extern time_t now;

bool ws_handle_talk_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data,    "$.talk.cmd");
   char *user = mg_json_get_str(msg_data,   "$.talk.user");
   char *privs = mg_json_get_str(msg_data,  "$.talk.privs");
   char *muted = mg_json_get_str(msg_data,  "$.talk.muted");
   double clones;
   char *ts = mg_json_get_str(msg_data,     "$.talk.ts");
   bool rv = false;
   bool tx;

   mg_json_get_bool(msg_data, "$.talk.tx", &tx);
   mg_json_get_num(msg_data, "$.talk.clones", &clones);

   if (!cmd) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "userinfo") == 0) {
      if (!user) {
         rv = true;
         goto cleanup;
      }

      Log(LOG_DEBUG, "ws.talk", "UserInfo: %s has privs '%s' (TX: %s, Muted: %s, clones: %.0f)", user, privs, (tx ? "true" : "false"), muted, clones);

      struct rr_user tmp = {0};

      snprintf(tmp.name, sizeof(tmp.name), "%s", user);
      snprintf(tmp.privs, sizeof(tmp.privs), "%s", privs);

      if (tx) {
         tmp.is_ptt = true;
      }

      if (muted && strcasecmp(muted, "true") == 0) {
         tmp.is_muted = true;
      }

      if (has_privs(&tmp, "admin")) {
         set_flag(&tmp, FLAG_ADMIN);
      } else if (has_privs(&tmp, "owner")) {
         set_flag(&tmp, FLAG_OWNER);
      }

      if (has_privs(&tmp, "muted")) {
         set_flag(&tmp, FLAG_MUTED);
         tmp.is_muted = true;
      }

      if (has_privs(&tmp, "ptt")) {
         set_flag(&tmp, FLAG_PTT);
      }

      if (has_privs(&tmp, "subscriber")) {
         set_flag(&tmp, FLAG_SUBSCRIBER);
      }

      if (has_privs(&tmp, "elmer")) {
         set_flag(&tmp, FLAG_ELMER);
      } else if (has_privs(&tmp, "noob")) {
         set_flag(&tmp, FLAG_NOOB);
      }

      if (has_privs(&tmp, "bot")) {
         set_flag(&tmp, FLAG_SERVERBOT);
      }

      if (has_privs(&tmp, "listener")) {
         set_flag(&tmp, FLAG_LISTENER);
      }

      if (has_privs(&tmp, "syslog")) {
         set_flag(&tmp, FLAG_SYSLOG);
      }

      if (has_privs(&tmp, "tx")) {
         set_flag(&tmp, FLAG_CAN_TX);
      }

      if (!userlist_add_or_update(&tmp)) {
         Log(LOG_CRIT, "ws", "OOM in ws_handle_talk_msg");
      } else {
         userlist_redraw_gtk();
      }
   } else if (cmd && strcasecmp(cmd, "msg") == 0) {
      char *from = mg_json_get_str(msg_data, "$.talk.from");
      char *data = mg_json_get_str(msg_data, "$.talk.data");
      char *msg_type = mg_json_get_str(msg_data, "$.talk.msg_type");

      if (msg_type && strcasecmp(msg_type, "pub") == 0) {
         ui_print("[%s] <%s> %s", get_chat_ts(), from, data);
      } else if (msg_type && strcasecmp(msg_type, "action") == 0) {
         ui_print("[%s] * %s %s", get_chat_ts(), from, data);
      }

      gui_window_t *win = gui_find_window(NULL, "main");
      GtkWidget *main_window = win->gtk_win;

      if (!gtk_window_is_active(GTK_WINDOW(main_window))) {
         gtk_window_set_urgency_hint(GTK_WINDOW(main_window), TRUE);
      }
      free(data);
      free(from);
      free(msg_type);
   } else if (cmd && strcasecmp(cmd, "join") == 0) {
      char *ip = mg_json_get_str(msg_data, "$.talk.ip");
      if (!user || !ip) {
         free(ip);
         goto cleanup;
      }
      
      struct rr_user *cptr = malloc(sizeof(struct rr_user));
      if (!cptr) {
         fprintf(stderr, "oom in ws_handle_chat_msg?!\n");
         goto cleanup;
      }
      memset(cptr, 0, sizeof(struct rr_user));
      snprintf(cptr->name, sizeof(cptr->name), "%s", user);
      Log(LOG_DEBUG, "ws.join", "New user %s has cptr:<%x>", user, cptr);
      userlist_add_or_update(cptr);
      ui_print("[%s] >>> %s connected to the radio <<<", get_chat_ts(), user);
      free(ip);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      char *reason = mg_json_get_str(msg_data, "$.talk.reason");
      if (!user || !reason) {
         free(reason);
         goto cleanup;
      }
      ui_print("[%s] >>> %s disconnected from the radio: %s (%.0f clones left)<<<", get_chat_ts(), user, reason ? reason : "No reason given", --clones);

      struct rr_user *cptr = userlist_find(user);
      if (!cptr) {
         free(reason);
         goto cleanup;
      }

      cptr->clones = clones;
      if (cptr->clones <= 0 ) {
         userlist_remove_by_name(cptr->name);
      }
      free(reason);
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = mg_json_get_str(msg_data, "$.talk.data");
      ui_print("[%s] >>> WHOIS %s", user);
      ui_print("[%s]   %s", whois_msg);
   } else {
      Log(LOG_DEBUG, "chat", "msg: %.*s", msg->data.len, msg->data.buf);
   }

cleanup:
   free(cmd);
   free(user);
   free(privs);
   free(muted);
   free(ts);
   return false;
}
