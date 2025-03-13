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

extern bool dying;		// in main.c
extern time_t now;		// in main.c
extern struct GlobalState rig;	// Global state

bool ws_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");
      return true;
   }

   return false;
}

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(struct mg_connection *sender_conn, struct mg_str *msg_data) {
   http_client_t *current = http_client_list;  // Iterate over all clients

   while (current != NULL) {
      // Only send to WebSocket clients. If sender_conn is set, exclude them
      if ((sender_conn == NULL) || (current->is_ws && current->conn != sender_conn)) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
      }
      current = current->next;
   }
}

void hash_to_hex(char *dest, const uint8_t *hash, size_t len) {
   for (size_t i = 0; i < len; i++) {
      sprintf(dest + (i * 2), "%02x", hash[i]);  // Convert each byte to hex
   }
   dest[len * 2] = '\0'; 			 // Null-terminate the string
}


bool ws_send_userlist(void) {
   char resp_buf[HTTP_WS_MAX_MSG+1];
   int len = mg_snprintf(resp_buf, sizeof(resp_buf), "{ \"talk\": { \"cmd\": \"names\", \"ts\": %lu, \"users\": [", now);

   int count = 0;

   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      len += mg_snprintf(resp_buf + len, sizeof(resp_buf) - len, "%s\"%s\"", count++ ? "," : "", cptr->user->name);
//      Log(LOG_DEBUG, "http.ws.noisy", "adding %s to userlist blob", cptr->user->name);
      cptr = cptr->next;
   }
   mg_snprintf(resp_buf + len, sizeof(resp_buf) - len, "] } }");
   struct mg_str ms = mg_str(resp_buf);
   ws_broadcast(NULL, &ms);
//   Log(LOG_DEBUG, "http.ws.noisy", "sending userlist: %s", resp_buf);
   
   return false;
}

void ws_blorp_userlist_cb(void *arg) {
   ws_send_userlist();
//   (void)arg;
}

bool ws_kick_client(http_client_t *cptr, const char *reason) {
   char resp_buf[HTTP_WS_MAX_MSG+1];
   struct mg_connection *c = cptr->conn;
   
   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth.noisy", "ws_kick_client for cptr <%x> has mg_conn <%x> and is invalid", cptr, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   memset(resp_buf, 0, sizeof(resp_buf));
   snprintf(resp_buf, sizeof(resp_buf), "{ \"auth\": { \"error\": \"Disconnected: %s.\" } }", reason);
   mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);
   mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE);
   http_remove_client(c);

   return false;
}

bool ws_kick_client_by_c(struct mg_connection *c, const char *reason) {
   char resp_buf[HTTP_WS_MAX_MSG+1];

   http_client_t *cptr = http_find_client_by_c(c);
   
   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth.noisy", "ws_kick_client_by_c for mg_conn <%x> has cptr <%x> and is invalid", c, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   return ws_kick_client(cptr, reason);
}

#define free_and_null(___x) do { if ((___x) != NULL) { free(___x); (___x) = NULL; } } while (0)

bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;

//     Log(LOG_DEBUG, "http.noisy", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);

   // Handle different message types...
   if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
      // Handle cat-related messages
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
      http_client_t *cptr = http_find_client_by_c(c);

      if (cptr == NULL) {
         Log(LOG_DEBUG, "chat", "talk parse, cptr == NULL, c: <%x>", c);
         return true;
      }

      char *token = mg_json_get_str(msg_data, "$.talk.token");
      char *cmd = mg_json_get_str(msg_data, "$.talk.cmd");
      char *data = mg_json_get_str(msg_data, "$.talk.data");

      if (cmd && data) {
         if (strcmp(cmd, "msg") == 0) {
            char msgbuf[HTTP_WS_MAX_MSG+1];
            struct mg_str mp;
            memset(msgbuf, 0, sizeof(msgbuf));
            snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\", \"ts\": %lu } }", cptr->user->name, data, now);
            mp = mg_str(msgbuf);

            // Update the sender's last_heard time
            cptr->last_heard = now;
            ws_broadcast(c, &mp);
         } else if (strcmp(cmd, "kick") == 0) {
            Log(LOG_INFO, "chat", "Kick command received, processing...");
         } else if (strcmp(cmd, "mute") == 0) {
            Log(LOG_INFO, "chat", "Mute command received, processing...");
         } else {
            Log(LOG_DEBUG, "chat.noisy", "Got unknown talk msg: |%.*s|", msg_data.len, msg_data.buf);
         }
      }
      free_and_null(token);
      free_and_null(cmd);
      free_and_null(data);
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
      char *user = mg_json_get_str(msg_data, "$.auth.user");

      if (strcmp(cmd, "login") == 0) {
         // Construct the response
         char resp_buf[HTTP_WS_MAX_MSG+1];

         memset(resp_buf, 0, sizeof(resp_buf));
         Log(LOG_DEBUG, "auth.noisy", "Login request from user %s", user);

         http_client_t *cptr = http_add_client(c, true);
         if (cptr == NULL) {
            free_and_null(cmd);
            free_and_null(user);
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
         free_and_null(cmd);
         free_and_null(user);
      } else if (strcmp(cmd, "logout") == 0) {
         char *token = mg_json_get_str(msg_data, "$.auth.token");

         Log(LOG_DEBUG, "auth.noisy", "Logout request from session token |%s|", (token != NULL ? token : "NONE"));
         ws_kick_client_by_c(c, "Logged out");
         free_and_null(token);
      } else if (strcmp(cmd, "pass") == 0) {
         char *pass = mg_json_get_str(msg_data, "$.auth.pass");
         char *token = mg_json_get_str(msg_data, "$.auth.token");

         if (pass == NULL || token == NULL) {
            Log(LOG_DEBUG, "auth", "auth pass command without without password <%x> / token <%x>", pass, token);
            ws_kick_client_by_c(c, "auth.pass message incomplete/invalid. Goodbye");
            free_and_null(pass);
            free_and_null(token);
            return false;
         }

         http_client_t *cptr = http_find_client_by_token(token);

         if (cptr == NULL) {
            Log(LOG_DEBUG, "auth", "Unable to find client in PASS parsing");
            http_dump_clients();
            free_and_null(pass);
            free_and_null(token);
            return true;
         }

         if (cptr->user == NULL) {
            Log(LOG_DEBUG, "auth", "cptr-> user == NULL!");
            http_dump_clients();
            free_and_null(pass);
            free_and_null(token);
            return true;
         }

         int login_uid = cptr->user->uid; // = const char *http_get_uname();
         if (login_uid < 0 || login_uid > HTTP_MAX_USERS) {
            Log(LOG_DEBUG, "auth.noisy", "Invalid uid for username %s", cptr->user->name);
            ws_kick_client(cptr, "Invalid login");
            free_and_null(pass);
            free_and_null(token);
            return true;
         }

         http_user_t *up = &http_users[login_uid];
         if (up == NULL) {
            Log(LOG_CRIT, "auth", "Uid %d returned NULL http_user_t", login_uid);
            free_and_null(pass);
            free_and_null(token);
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
         free_and_null(pass);
         free_and_null(token);
      }
   }

   return false;
}
