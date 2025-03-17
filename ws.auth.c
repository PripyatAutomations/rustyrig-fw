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
#include "websocket.h"
extern struct GlobalState rig;	// Global state

bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL) {
      Log(LOG_DEBUG, "http.ws", "auth_msg: got msg <%x> c <%x>", msg, c);
      return true;
   }

   if (msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "auth_msg: got msg <%x> with no data ptr");
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *token = mg_json_get_str(msg_data, "$.auth.token");
   char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
   char *user = mg_json_get_str(msg_data, "$.auth.user");

   // Must always send a command and username during auth
   if (cmd == NULL || (user == NULL && token == NULL)) {
      return true;
   }

   if (strcmp(cmd, "login") == 0) {
      char resp_buf[HTTP_WS_MAX_MSG+1];
      memset(resp_buf, 0, sizeof(resp_buf));

      Log(LOG_DEBUG, "auth.noisy", "Login request from user %s", user);

      http_client_t *cptr = http_add_client(c, true);
      if (cptr == NULL) {
         free(cmd);
         free(user);
         return true;
      }

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
      free(cmd);
      free(user);
   } else if (strcmp(cmd, "logout") == 0) {
      char *token = mg_json_get_str(msg_data, "$.auth.token");

      Log(LOG_DEBUG, "auth.noisy", "Logout request from session token |%s|", (token != NULL ? token : "NONE"));
      ws_kick_client_by_c(c, "Logged out");
      free(token);
   } else if (strcmp(cmd, "pass") == 0) {
      char *pass = mg_json_get_str(msg_data, "$.auth.pass");
      char *token = mg_json_get_str(msg_data, "$.auth.token");

      if (pass == NULL || token == NULL) {
         Log(LOG_DEBUG, "auth", "auth pass command without without password <%x> / token <%x>", pass, token);
         ws_kick_client_by_c(c, "auth.pass message incomplete/invalid. Goodbye");
         free(pass);
         free(token);
         return false;
      }

      http_client_t *cptr = http_find_client_by_token(token);

      if (cptr == NULL) {
         Log(LOG_DEBUG, "auth", "Unable to find client in PASS parsing");
         http_dump_clients();
         free(pass);
         free(token);
         return true;
      }

      if (cptr->user == NULL) {
         Log(LOG_DEBUG, "auth", "cptr-> user == NULL!");
         ws_kick_client(cptr, "Invalid login/password");
         free(pass);
         free(token);
         return true;
      }

      int login_uid = cptr->user->uid;
      if (login_uid < 0 || login_uid > HTTP_MAX_USERS) {
         Log(LOG_DEBUG, "auth.noisy", "Invalid uid for username %s", cptr->user->name);
         ws_kick_client(cptr, "Invalid login/passowrd");
         free(pass);
         free(token);
         return true;
      }

      http_user_t *up = &http_users[login_uid];
      if (up == NULL) {
         Log(LOG_CRIT, "auth", "Uid %d returned NULL http_user_t", login_uid);
         free(pass);
         free(token);
         return true;
      }

      if (strcmp(up->pass, pass) == 0) {
         cptr->authenticated = true;

         // Store some timestamps such as when user joined & session will forcibly expire
         cptr->session_start = now;
         cptr->last_heard = now;
         cptr->session_expiry = now + HTTP_SESSION_LIFETIME;

         // Send last message (AUTHORIZED) of the login sequence to let client know they are logged in
         char resp_buf[HTTP_WS_MAX_MSG+1];
         memset(resp_buf, 0, sizeof(resp_buf));
         snprintf(resp_buf, sizeof(resp_buf),
                  "{ \"auth\": { \"cmd\": \"authorized\", \"user\": \"%s\", \"token\": \"%s\", \"ts\": %lu } }",
                  user, token, now);
         mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

         // blorp out a join to all chat users
         memset(resp_buf, 0, sizeof(resp_buf));
         snprintf(resp_buf, sizeof(resp_buf),
                  "{ \"talk\": { \"cmd\": \"join\", \"user\": \"%s\", \"ts\": %lu } }",
                  user, now);
         struct mg_str ms = mg_str(resp_buf);
         ws_broadcast(NULL, &ms);
         Log(LOG_INFO, "auth", "User %s on cptr <%x> logged in with privs: %s",
             cptr->user->name, cptr, http_users[cptr->user->uid].privs);
      } else {
         Log(LOG_INFO, "auth", "User %s on cptr <%x> gave wrong password. Kicking!", cptr->user, cptr);
         ws_kick_client(cptr, "Invalid login/password");
      }
      free(pass);
      free(token);
   }
   return false;
}
