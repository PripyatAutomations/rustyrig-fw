//
// rrclient/ws.auth.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/core.h>
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
//#include <mod.ui.gtk3/gtk.core.h>
#include <rrclient/connman.h>
//#include <rrclient/audio.h>
#include <rrclient/userlist.h>
#include <librrprotocol/rrprotocol.h>

extern dict *cfg;		// config.c
extern time_t now;
extern const char *server_name;                         // connman.c XXX: to remove ASAP for multiserver

// XXX: This needs moved into the ws_conn
extern char session_token[HTTP_TOKEN_LEN+1];

bool ws_handle_client_auth_msg(struct mg_connection *c, dict *d) {
   bool rv = false;

   if (!c || !d) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg mg_conn:<%x> msg:<%x>", c, d);
      return true;
   }

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;

   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   char *cmd = dict_get(d, "auth.cmd", NULL);
   char *nonce = dict_get(d, "auth.nonce", NULL);
   char *user = dict_get(d, "auth.user", NULL);
   time_t ts = dict_get_time_t(d, "auth.ts", now);

//   ui_print("[%s] => cmd: '%s', nonce: %s, user: %s", get_chat_ts(ts), cmd, nonce, user);

   // Must always send a command and username during auth
   if (!cmd || !user) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "challenge") == 0) {
      char *token = dict_get(d, "auth.token", NULL);
      time_t ts = dict_get_time_t(d, "auth.ts", now);

      if (token) {
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
//         ui_print("[%s] ?? Got CHALLENGE without valid token!", get_chat_ts(ts));
         goto cleanup;
      }

//      ui_print("[%s] *** Sending PASSWD ***", get_chat_ts(ts));
      const char *login_pass = get_server_property(server_name, "server.pass");

      ws_send_passwd(c, user, login_pass, nonce);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
//      ui_print("[%s] *** Authorized ***", get_chat_ts(ts));
//      userlist_redraw_gtk();
      // XXX: Set online state
   }

cleanup:
   return rv;
}

bool ws_send_login(struct mg_connection *c, const char *login_user) {
   if (!c || !login_user) {
      Log(LOG_DEBUG, "ws.auth", "send_login c:<%x> login_user:<%x> |%s|", c, login_user, login_user);
      return true;
   }

//   ui_print("[%s] *** Sending LOGIN ***", get_chat_ts(now));
   const char *jp = dict2json_mkstr(
      VAL_STR, "auth.cmd", "login",
      VAL_STR, "auth.user", login_user);

   int ret = mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

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

   const char *jp = dict2json_mkstr(
      VAL_STR, "auth.cmd", "pass",
      VAL_STR, "auth.user", user,
      VAL_STR, "auth.pass", temp_pw,
      VAL_STR, "auth.token", session_token);
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);
   free(temp_pw);
   return false;
}

bool ws_send_logout(struct mg_connection *c, const char *user, const char *token) {
   if (!user || !token || !c) {
      Log(LOG_DEBUG, "ws.auth", "send_logout c:<%x> user:<%x> |%s|", c, user, user);
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   const char *jp = dict2json_mkstr(
      VAL_STR, "auth.cmd", "logout",
      VAL_STR, "auth.user", user,
      VAL_STR, "auth.token", token);
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   return false;
}

bool ws_send_hello(struct mg_connection *c) {
   if (!c) {
      Log(LOG_DEBUG, "ws.auth", "send_hello c:<%x>", c);
      return true;
   }
   char msgbuf[512];
   const char *codec = "mulaw";
   int rate = 16000;
   const char *jp = dict2json_mkstr(VAL_STR, "hello", VERSION);
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   return false;
}
