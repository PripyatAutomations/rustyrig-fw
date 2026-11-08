//
// rrclient/ws.auth.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "build_config.h"       /* VERSION/HARDWARE come from the client profile. */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
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
      if (strcmp(dict_get(d, "auth.method", ""), "tls-password-v1") ||
          !ws_send_passwd(cptr, user, login_pass, nonce)) {
         Log(LOG_WARN, "auth.ws", "Server authentication method is unsupported or TLS is unverified");
         event_emit_dict("auth.error", cptr, d);
         rv = false;
         goto cleanup;
      }
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
/* No reusable password verifier or legacy SHA-1 wire response is sent.
 * The initial password is delivered once over an authenticated TLS channel.
 */
bool ws_send_passwd(rrconn_t *cptr, const char *user, const char *passwd, const char *nonce) {
   if (!cptr || !user || !passwd || !nonce || !*nonce ||
       strlen(passwd) > 256 || !cptr->native_transport ||
       !(rr_transport_capabilities(cptr->native_transport) & RR_TRANSPORT_TLS_AUTHENTICATED)) {
      Log(LOG_WARN, "auth", "TLS-verified connection and valid password are required");
      return false;
   }
   dict *message = dict_new();
   if (!message) return false;
   dict_add(message, "msg.type", "auth");
   dict_add(message, "auth.cmd", "pass");
   dict_add(message, "auth.method", "tls-password-v1");
   dict_add(message, "auth.user", user);
   dict_add(message, "auth.pass", passwd);
   dict_add(message, "auth.token", cptr->token);
   bool sent = ws_send_dict(NULL, cptr, message, WEBSOCKET_OP_TEXT);
   dict_free(message);
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
