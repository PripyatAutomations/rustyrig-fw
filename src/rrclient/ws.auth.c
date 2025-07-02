//
// gtk-client/ws.auth.c
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
extern char *negotiated_codecs;		// ws.c

bool ws_handle_auth_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   bool rv = false;

   if (!c || !msg) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg:<%x> mg_conn:<%x>", msg, c);
      return true;
   }

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   if (!msg->data.buf) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
   char *nonce = mg_json_get_str(msg_data, "$.auth.nonce");
   char *user = mg_json_get_str(msg_data, "$.auth.user");
//   ui_print("[%s] => cmd: '%s', nonce: %s, user: %s", get_chat_ts(), cmd, nonce, user);

   // Must always send a command and username during auth
   if (!cmd || !user) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "challenge") == 0) {
      char *token = mg_json_get_str(msg_data, "$.auth.token");

      if (token) {
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
         ui_print("[%s] ?? Got CHALLENGE without valid token!", get_chat_ts());
         goto cleanup;
      }

      ui_print("[%s] *** Sending PASSWD ***", get_chat_ts());
      const char *login_pass = get_server_property(active_server, "server.pass");

      ws_send_passwd(c, user, login_pass, nonce);
      free(token);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
      ui_print("[%s] *** Authorized ***", get_chat_ts());

      if (!negotiated_codecs) {
         Log(LOG_CRIT, "ws.auth", "No negotiated codecs yet, skipping set initial codec");
         goto cleanup;
      } else {
         Log(LOG_DEBUG, "ws.auth", "Negotiated codecs: %s", negotiated_codecs);
      }

      char first_codec[5];
      // Make sure it's all NULLs, so we'll get null terminated string
      memset(first_codec, 0, 5);
      // Copy the *first* codec of the negotiated set, as it's our most preferred.
      memcpy(first_codec, negotiated_codecs, 4);
      Log(LOG_CRAZY, "ws.media", ">> setting initial rx codec to first_codec: %s <<", first_codec);
      ws_select_codec(c, first_codec, false);
   }

cleanup:
   free(cmd);
   free(nonce);
   free(user);
   return rv;
}
