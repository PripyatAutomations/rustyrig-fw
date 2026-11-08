//
// rrclient/ws.auth.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern dict *cfg;
extern time_t now;
extern const char *server_name;
extern char session_token[HTTP_TOKEN_LEN + 1];  // Compatibility view of the selected native connection

bool ws_handle_client_auth_msg(rrconn_t *cptr, dict *d) {
   bool rv = true;

   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg from cptr:<%p> msg:<%p>", cptr, d);

      return false;
   }

   char *ip = cptr->user_ip;
   int port = cptr->user_port;

   const char *cmd = dict_get(d, "auth.cmd", NULL);
   const char *nonce = dict_get(d, "auth.nonce", NULL);
   const char *user = dict_get(d, "auth.user", NULL);
   time_t ts = dict_get_time_t(d, "auth.ts", now);

   // An auth error (e.g. kicked: invalid login/password) is fatal for this
   // session. Emit an event for the program/UI and don't process further.
   // PARITY: www/js/webui.auth.js webui_parse_auth_msg() error branch
   const char *error = dict_get(d, "auth.error", NULL);

   if (error) {
      event_emit_dict("auth.error", cptr, d);
      rv = true;
      goto cleanup;
   }

   // Must always send a command and username during auth
   if (!cmd || !user) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "challenge") == 0) {
      const char *token = dict_get(d, "auth.token", NULL);
      time_t ts = dict_get_time_t(d, "auth.ts", now);

      if (token) {
         snprintf(cptr->token, sizeof(cptr->token), "%s", token);
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
         Log(LOG_CRIT, "rrproto.auth", "CHALLENGE with invalid token from %s", server_name);
         goto cleanup;
      }
      const char *login_pass = cptr->server ? cptr->server->pass : get_server_property(server_name, "server.pass");
      Log(LOG_AUDIT, "auth.ws", "Got login challenge from server %s", server_name);
      ws_send_passwd(cptr, user, login_pass, nonce);
      event_emit_dict("logging-in", cptr, d);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
      cptr->authenticated = true;
      snprintf(cptr->chatname, sizeof(cptr->chatname), "%s", user);
      event_emit_dict("authorized", cptr, d);
   }

cleanup:

   return rv;
}

bool ws_send_login(rrconn_t *cptr, const char *login_user) {
   if (!cptr || !login_user) {
      Log(LOG_DEBUG, "auth.ws", "send_login cptr:<%p> login_user:<%p> |%s|", cptr, login_user, login_user);

      return false;
   }
   Log(LOG_INFO, "rrproto.auth", "Sending initial LOGIN!");
   dict *auth_msg = dict_new();

   if (!auth_msg) {
      return false;
   }
   dict_add(auth_msg, "msg.type", "auth");
   dict_add(auth_msg, "auth.cmd", "login");
   dict_add(auth_msg, "auth.user", login_user);
   bool sent = ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
   dict_free(auth_msg);

   return sent;
}

// Hashes the user stored password with the server nonce and returns it
bool ws_send_passwd(rrconn_t *cptr, const char *user, const char *passwd, const char *nonce) {
   if (!cptr || !user || !passwd || !nonce) {
      Log(LOG_CRIT, "auth", "ws_send_passwd with invalid parameters, cptr:<%p> user:<%p> passwd:<%p> nonce:<%p>", cptr, user, passwd, nonce);

      return false;
   }

   char *hashed_pw = hash_passwd(passwd);
   char *temp_pw = NULL;

   if (hashed_pw) {
      temp_pw = compute_wire_password(hashed_pw, nonce);
      explicit_bzero(hashed_pw, strlen(hashed_pw));
      free((void *)hashed_pw);
      hashed_pw = NULL;
   }

   if (!temp_pw) {
      Log(LOG_CRIT, "auth", "Failed to hash session password");

      return false;
   }

   dict *auth_msg = dict_new();

   if (!auth_msg) {
      explicit_bzero(temp_pw, strlen(temp_pw));
      free(temp_pw);

      return false;
   }
   dict_add(auth_msg, "msg.type", "auth");
   dict_add(auth_msg, "auth.cmd", "pass");
   dict_add(auth_msg, "auth.user", user);
   dict_add(auth_msg, "auth.pass", temp_pw);
   dict_add(auth_msg, "auth.token", cptr->token);
   bool sent = ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
   dict_free(auth_msg);
   explicit_bzero(temp_pw, strlen(temp_pw));
   free(temp_pw);

   return sent;
}

bool ws_send_logout(rrconn_t *cptr, const char *user, const char *token) {
   if (!user || !token || !cptr) {
      Log(LOG_DEBUG, "auth.ws", "send_logout cptr:<%p> user:<%p> |%s|", cptr, user, user);

      return false;
   }

   dict *auth_msg = dict_new();

   if (!auth_msg) {
      return false;
   }
   dict_add(auth_msg, "msg.type", "auth");
   dict_add(auth_msg, "auth.cmd", "logout");
   dict_add(auth_msg, "auth.user", user);
   dict_add(auth_msg, "auth.token", token);
   bool sent = ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
   dict_free(auth_msg);

   return sent;
}

bool ws_send_hello(rrconn_t *cptr) {
   if (!cptr) {
      Log(LOG_DEBUG, "auth.ws", "send_hello cptr:<%p>", cptr);

      return false;
   }
   char msgbuf[512];
   const char *codec = "mu08,mu08";
   int rate = 16000;
   dict *hello = dict_new();

   if (!hello) {
      return false;
   }
   dict_add(hello, "msg.type", "hello");
   dict_add(hello, "hello.swver", VERSION);
   dict_add(hello, "hello.hwver", HARDWARE);
   // Connections which aren't ordinary users (e.g. webcam feeds) announce a
   // role; the server flags them and keeps them out of the user lists.
   // PARITY: librrprotocol/srv.http.c (hello.role parsing)
   const char *role = cfg_get("client.role");

   if (role && role[0] != '\0') {
      dict_add(hello, "hello.role", (char *)role);
   }
   bool sent = ws_send_dict(NULL, cptr, hello, WEBSOCKET_OP_TEXT);
   dict_free(hello);

   return sent;
}
