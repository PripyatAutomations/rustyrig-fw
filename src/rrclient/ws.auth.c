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
#include "rrclient/gtk.core.h"
#include "rrclient/connman.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
extern time_t now;
extern const char *server_name;                         // connman.c XXX: to remove ASAP for multiserver

// XXX: This needs moved into the ws_conn
extern char session_token[HTTP_TOKEN_LEN+1];

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
      // XXX: We should count these and if too many errors in a period of time, kick the user
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
      const char *login_pass = get_server_property(server_name, "server.pass");

      ws_send_passwd(c, user, login_pass, nonce);
      free(token);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
      ui_print("[%s] *** Authorized ***", get_chat_ts());
      userlist_redraw_gtk();
      // XXX: Set online state
   }

cleanup:
   free(cmd);
   free(nonce);
   free(user);
   return rv;
}

bool ws_send_login(struct mg_connection *c, const char *login_user) {
   if (!c || !login_user) {
      return true;
   }

   ui_print("[%s] *** Sending LOGIN ***", get_chat_ts());
   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512,
                 "{ \"auth\": {"
                 " \"cmd\": \"login\", "
                 " \"user\": \"%s\""
                 "} }", login_user);

   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   if (ret < 0) {
      Log(LOG_DEBUG, "auth", "ws_send_login: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

// Hashes the user stored password with the server nonce and returns it
bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *nonce) {
   if (!c || !user || !passwd || !nonce) {
      Log(LOG_CRIT, "auth", "ws_send_passwd with invalid parameters, c:<%x> user:<%x> passwd:<%x> nonce:<%x>", c, user, passwd, nonce);
      return true;
   }

   char *temp_pw = compute_wire_password(hash_passwd(passwd), nonce);
   if (!temp_pw) {
      Log(LOG_CRIT, "auth", "Failed to hash session password (nonce: |%s|)", nonce);
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512,
                 "{ \"auth\": {"
                 " \"cmd\": \"pass\", "
                 " \"user\": \"%s\","
                 " \"pass\": \"%s\","
                 " \"token\": \"%s\""
                 "} }", user, temp_pw, session_token);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   free(temp_pw);
   return false;
}

bool ws_send_logout(struct mg_connection *c, const char *user, const char *token) {
   if (!user || !token || !c) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512,
                 "{ \"auth\": {"
                 " \"cmd\": \"logout\", "
                 " \"user\": \"%s\","
                 " \"token\": \"%s\""
                 "} }", user, token);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   return false;
}

bool ws_send_hello(struct mg_connection *c) {
   if (!c) {
      return true;
   }
   char msgbuf[512];
   const char *codec = "mulaw";
   int rate = 16000;
   memset(msgbuf, 0, sizeof(msgbuf));
   snprintf(msgbuf, sizeof(msgbuf), "{ \"hello\": \"rrclient %s\", \"codec\": \"%s\", \"rate\": %d }", VERSION, codec, rate);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   return false;
}
