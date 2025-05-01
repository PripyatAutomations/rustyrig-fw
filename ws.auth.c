#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "ws.h"
extern struct GlobalState rig;	// Global state

bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = false;

   if (c == NULL || msg == NULL) {
      Log(LOG_DEBUG, "http.ws", "auth_msg: got msg <%x> c <%x>", msg, c);
      return true;
   }

   if (msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "auth_msg: got msg <%x> with no data ptr");
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
   char *pass = mg_json_get_str(msg_data, "$.auth.pass");
   char *token = mg_json_get_str(msg_data, "$.auth.token");
   char *user = mg_json_get_str(msg_data, "$.auth.user");

   // Must always send a command and username during auth
   if (cmd == NULL || (user == NULL && token == NULL)) {
      rv = true;
      goto cleanup;
   }

   if (strcasecmp(cmd, "login") == 0) {
      char resp_buf[HTTP_WS_MAX_MSG+1];
      memset(resp_buf, 0, sizeof(resp_buf));

      Log(LOG_DEBUG, "auth.noisy", "Login request from user %s", user);

      http_client_t *cptr = http_add_client(c, true);
      if (cptr == NULL) {
         rv = true;
         goto cleanup;
      }

      // search for user
      for (int i = 0; i < HTTP_MAX_USERS; i++) {
         if (strcasecmp(http_users[i].name, user) == 0) {
            cptr->user = &http_users[i];
            break;
         }
      }

      snprintf(resp_buf, sizeof(resp_buf),
               "{ \"auth\": { \"cmd\": \"challenge\", \"nonce\": \"%s\", \"user\": \"%s\", \"token\": \"%s\" } }",
               cptr->nonce, user, cptr->token);
      mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

      Log(LOG_DEBUG, "auth.noisy", "Sending login challenge |%s| to user at cptr <%x>", cptr->nonce, cptr);
   } else if (strcasecmp(cmd, "logout") == 0) {

      Log(LOG_DEBUG, "auth.noisy", "Logout request from session token |%s|", (token != NULL ? token : "NONE"));
      ws_kick_client_by_c(c, "Logged out");
   } else if (strcasecmp(cmd, "pass") == 0) {
      bool guest = false;

      if (pass == NULL || token == NULL) {
         Log(LOG_DEBUG, "auth", "auth pass command without without password <%x> / token <%x>", pass, token);
         ws_kick_client_by_c(c, "auth.pass message incomplete/invalid. Goodbye");
         rv = true;
         goto cleanup;
      }

      http_client_t *cptr = http_find_client_by_token(token);

      if (cptr == NULL) {
         Log(LOG_DEBUG, "auth", "Unable to find client in PASS parsing");
         http_dump_clients();
         rv = true;
         goto cleanup;
      }

      // Save the remote IP
      char ip[INET6_ADDRSTRLEN];  // Buffer to hold IPv4 or IPv6 address
      int port = c->rem.port;
      if (c->rem.is_ip6) {
         inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
      } else {
         inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
      }

      if (cptr->user == NULL) {
         Log(LOG_DEBUG, "auth", "cptr-> user == NULL handling conn from ip %s:%d", ip, port);
         ws_kick_client(cptr, "Invalid login/password");
         rv = true;
         goto cleanup;
      }

      int login_uid = cptr->user->uid;
      if (login_uid < 0 || login_uid > HTTP_MAX_USERS) {
         Log(LOG_DEBUG, "auth.noisy", "Invalid uid for username %s from IP %s:%d", cptr->user->name, ip, port);
         ws_kick_client(cptr, "Invalid login/passowrd");
         rv = true;
         goto cleanup;
      }

      http_user_t *up = &http_users[login_uid];
      if (up == NULL) {
         Log(LOG_CRIT, "auth", "Uid %d returned NULL http_user_t", login_uid);
         rv = true;
         goto cleanup;
      }

      // Deal with double-hashed (reply-protected) responses
      char *nonce = cptr->nonce;
      if (nonce == NULL) {
         Log(LOG_CRIT, "auth", "No nonce for user %d", login_uid);
         rv = true;
         goto cleanup;
      }
//      char *temp_pw = compute_wire_password(up->pass, nonce);
//      Log(LOG_INFO, "auth", "Saved: %s, hashed (server): %s, received: %s", up->pass, temp_pw, pass);

//      if (strcmp(temp_pw, pass) == 0) {
      if (strcasecmp(up->pass, pass) == 0) {
         // special handling for guests; we generate a random # prefix for their name
         if (strcasecmp(up->name, "guest") == 0) {
            cptr->guest_id = generate_random_number(4);
            guest = true;
         }

         cptr->authenticated = true;

         // Store some timestamps such as when user joined & session will forcibly expire
         cptr->session_start = now;
         cptr->last_heard = now;
         cptr->session_expiry = now + HTTP_SESSION_LIFETIME;

         // Send last message (AUTHORIZED) of the login sequence to let client know they are logged in
         char resp_buf[HTTP_WS_MAX_MSG+1];
         memset(resp_buf, 0, sizeof(resp_buf));

         // add suffix (numeric) for guest
         if (guest) {
            snprintf(resp_buf, sizeof(resp_buf),
                     "{ \"auth\": { \"cmd\": \"authorized\", \"user\": \"%s%04d\", \"token\": \"%s\", \"ts\": %lu, \"privs\": \"%s\" } }",
                     user, cptr->guest_id, token, now, cptr->user->privs);
         } else {
            snprintf(resp_buf, sizeof(resp_buf),
                     "{ \"auth\": { \"cmd\": \"authorized\", \"user\": \"%s\", \"token\": \"%s\", \"ts\": %lu, \"privs\": \"%s\" } }",
                     user, token, now, cptr->user->privs);
         }
         mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

         // blorp out a join to all chat users
         memset(resp_buf, 0, sizeof(resp_buf));

         // add suffix (numeric) for guests
         if (guest) {
            snprintf(resp_buf, sizeof(resp_buf),
                     "{ \"talk\": { \"cmd\": \"join\", \"user\": \"%s%04d\", \"ts\": %lu, \"ip\": \"%s\" } }",
                     user, cptr->guest_id, now, ip);
         } else {
            snprintf(resp_buf, sizeof(resp_buf),
                     "{ \"talk\": { \"cmd\": \"join\", \"user\": \"%s\", \"ts\": %lu, \"ip\": \"%s\" } }",
                     user, now, ip);
         }
         struct mg_str ms = mg_str(resp_buf);
         ws_broadcast(NULL, &ms);
         Log(LOG_AUDIT, "auth", "User %s on cptr <%x> logged in from IP %s (port %d) with privs: %s",
             cptr->user->name, cptr, ip, port, http_users[cptr->user->uid].privs);
      } else {
         Log(LOG_AUDIT, "auth", "User %s on cptr <%x> from IP %s (port %d) gave wrong password. |%s| != |%s| Kicking!", cptr->user, cptr, ip, port, pass, up->pass);
         ws_kick_client(cptr, "Invalid login/password");
      }
   }

cleanup:
   free(cmd);
   free(pass);
   free(token);
   free(user);
   return rv;
}
