//
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
#include "rustyrig/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
extern struct mg_mgr mgr;
extern bool ws_connected;
extern bool ws_tx_connected;
extern struct mg_connection *ws_conn, *ws_tx_conn;
extern bool server_ptt_state;
extern const char *tls_ca_path;
extern struct mg_str tls_ca_path_str;
extern bool cfg_show_pings;
extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern dict *servers;

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

      Log(LOG_DEBUG, "ws.talk", "[%s] UserInfo: %s has privs '%s' (TX: %s, Muted: %s, clones: %.0f)", get_chat_ts(), user, privs, (tx ? "true" : "false"), muted, clones);
      struct rr_user *cptr = find_or_create_client(user);

      if (cptr) {
         memset(cptr, 0, sizeof(struct rr_user));
         snprintf(cptr->name, sizeof(cptr->name), "%s", user);
         snprintf(cptr->privs, sizeof(cptr->privs), "%s", privs);
         if (tx) {
            cptr->is_ptt = true;
         }
         if (muted && strcasecmp(muted, "true") == 0) {
            cptr->is_muted = muted;
         }
         if (has_privs(cptr, "admin")) {
            set_flag(cptr, FLAG_ADMIN);
         } else if (has_privs(cptr, "owner")) {
            set_flag(cptr, FLAG_OWNER);
         }
         if (has_privs(cptr, "muted")) {
            set_flag(cptr, FLAG_MUTED);
            cptr->is_muted = true;
         }
         if (has_privs(cptr, "ptt")) {
            set_flag(cptr, FLAG_PTT);
         }
         if (has_privs(cptr, "subscriber")) {
            set_flag(cptr, FLAG_SUBSCRIBER);
         }
         if (has_privs(cptr, "elmer")) {
            set_flag(cptr, FLAG_ELMER);
         } else if (has_privs(cptr, "noob")) {
            set_flag(cptr, FLAG_NOOB);
         }
         if (has_privs(cptr, "bot")) {
            set_flag(cptr, FLAG_SERVERBOT);
         }
         if (has_privs(cptr, "listener")) {
            set_flag(cptr, FLAG_LISTENER);
         }
         if (has_privs(cptr, "syslog")) {
            set_flag(cptr, FLAG_SYSLOG);
         }
         if (has_privs(cptr, "tx")) {
            set_flag(cptr, FLAG_CAN_TX);
         }
         userlist_resync_all();
      } else {
         Log(LOG_CRIT, "ws", "OOM in ws_handle_talk_msg");
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
      
      struct rr_user *cptr = find_or_create_client(user);
      ui_print("[%s] >>> %s connected to the radio <<<", get_chat_ts(), user);
      Log(LOG_DEBUG, "ws.join", "New user %s has cptr:<%x>", user, cptr);
      free(ip);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      char *reason = mg_json_get_str(msg_data, "$.talk.reason");
      if (!user || !reason) {
         free(reason);
         goto cleanup;
      }
      ui_print("[%s] >> %s disconnected from the radio: %s (%.0f clones left)<<<", get_chat_ts(), user, reason ? reason : "No reason given", --clones);

      struct rr_user *cptr = find_client(user);
      if (!cptr) {
         free(reason);
         goto cleanup;
      }

      cptr->clones = clones;
      if (cptr->clones <= 0 ) {
         delete_client(cptr);
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
