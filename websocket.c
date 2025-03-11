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

// Our websocket client list for broadcasting messages & chat
static struct ws_client *client_list = NULL;

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
      // Only send to WebSocket clients
      if (current->is_ws && current->conn != sender_conn) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
      }
      current = current->next;
   }
}

static int generate_nonce(char *buffer, size_t length) {
   static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   size_t i;

   if (length < 1) return 0;  // Ensure valid length

   for (i = 0; i < (length - 2); i++) {
      buffer[i] = base64_chars[rand() % 64];  // Directly assign base64 characters
   }
   
   buffer[length] = '\0';  // Null terminate
   return length;
}

void hash_to_hex(char *dest, const uint8_t *hash, size_t len) {
   for (size_t i = 0; i < len; i++) {
      sprintf(dest + (i * 2), "%02x", hash[i]);  // Convert each byte to hex
   }
   dest[len * 2] = '\0'; 			 // Null-terminate the string
}

bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;

   // Handle different message types...
   if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
      // Handle cat-related messages
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
      Log(LOG_DEBUG, "chat.noisy", "Got message from client: |%.*s|", msg_data.len, msg_data.buf);
      char *cmd = mg_json_get_str(msg_data, "$.talk.cmd");
      char *data = mg_json_get_str(msg_data, "$.talk.data");
      char *token = mg_json_get_str(msg_data, "$.talk.token");

      http_client_t *cptr = http_find_client_by_token(token);

      if (cmd && data) {
         // Handle the 'msg', 'kick', and 'mute' commands
         if (strcmp(cmd, "msg") == 0) {
            char msgbuf[512];
            struct mg_str mp;
            memset(msgbuf, 0, sizeof(msgbuf));
            snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\" } }", cptr->user, data);
            Log(LOG_DEBUG, "chat.noisy", "sending |%s| (%lu of %lu max bytes) to client", msgbuf, strlen(msgbuf), sizeof(msgbuf));
            mp = mg_str(msgbuf);
            ws_broadcast(c, &mp);
         } else if (strcmp(cmd, "kick") == 0) {
            Log(LOG_INFO, "chat", "Kick command received, processing...");
         } else if (strcmp(cmd, "mute") == 0) {
            Log(LOG_INFO, "chat", "Mute command received, processing...");
         } else {
            Log(LOG_WARN, "chat", "Unknown talk command: %s", cmd);
         }

         // Free the allocated memory after use
         free(cmd);
         free(data);
      }
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
      char *user = mg_json_get_str(msg_data, "$.auth.user");

      if (strcmp(cmd, "login") == 0) {
         // Construct the response
         char resp_buf[512];
         char nonce[HTTP_TOKEN_LEN+1];
         srand((unsigned int)time(NULL));
         memset(resp_buf, 0, sizeof(resp_buf));
         memset(nonce, 0, sizeof(nonce));
         char token[HTTP_TOKEN_LEN+1];

         // Generate the nonce & token
         generate_nonce(nonce, sizeof(nonce));
         generate_nonce(token, sizeof(token));

         Log(LOG_DEBUG, "auth.noisy", "Login request from user %s", user);

         http_client_t *cptr = http_add_client(c, true);
         memcpy(cptr->nonce, nonce, sizeof(cptr->nonce));
         memcpy(cptr->token, token, sizeof(cptr->token));

         // Safely format the response message without overwriting
         snprintf(resp_buf, sizeof(resp_buf), 
                  "{ \"auth\": { \"cmd\": \"challenge\", \"nonce\": \"%s\", \"user\": \"%s\", \"token\": \"%s\" } }", 
                  nonce, user, token);

         // Send the response message
         mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);
         Log(LOG_DEBUG, "auth.noisy", "Sending login challenge |%s| to user", resp_buf);
      } else if (strcmp(cmd, "logout") == 0) {
         char *token = mg_json_get_str(msg_data, "$.auth.token");
         http_client_t *cptr = NULL;

         if (token != NULL && token[0] != '\0') {
            http_find_client_by_token(token);
         }

         Log(LOG_DEBUG, "auth.noisy", "Logout request from session token %s", cptr->token);
      } else if (strcmp(cmd, "pass") == 0) {
         char *pass = mg_json_get_str(msg_data, "$.auth.pass");
         char *req_token = mg_json_get_str(msg_data, "$.auth.token");
         char *nonce = NULL;
         char *token = mg_json_get_str(msg_data, "$.auth.token");

         Log(LOG_DEBUG, "auth.noisy", "mjs: auth - token: %s", token);
         http_client_t *cptr = http_find_client_by_token(token);

         if (cptr == NULL) {
            Log(LOG_DEBUG, "auth", "Unable to find client in PASS parsing");
         }

         nonce = cptr->nonce;
         char resp_buf[512];
         memset(resp_buf, 0, sizeof(resp_buf));

         // Safely format the response message without overwriting
         snprintf(resp_buf, sizeof(resp_buf), 
                  "{ \"auth\": { \"cmd\": \"authorized\", \"user\": \"%s\", \"token\": \"%s\" } }", 
                  user, token);

         // Send the response message
         mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);
         Log(LOG_DEBUG, "auth.noisy", "Sending |%s| to user", resp_buf);

         // User is sending hashed password
         Log(LOG_DEBUG, "auth.noisy", "PASS from user %s with token %s (pass: |%s|)", user, token, pass);
//         compute_wire_password(password_hash, nonce, final_hash);
//         Log(LOG_DEBUG, "auth.noisy", "My computed result for pass |%s| with nonce |%s| is |%s|", password_hash, nonce, final_hash);
      }
   }

   return false;
}
