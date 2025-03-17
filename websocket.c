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

bool ws_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");
      return true;
   }

   Log(LOG_DEBUG, "http.ws", "WebSocket init completed succesfully");
   return false;
}

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(struct mg_connection *sender, struct mg_str *msg_data) {
   if (sender == NULL || msg_data == NULL) {
      return;
   }

   http_client_t *current = http_client_list;
   while (current != NULL) {
      if ((sender == NULL) || (current->is_ws && current->conn != sender)) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
      }
      current = current->next;
   }
}

bool ws_send_userlist(void) {
   char resp_buf[HTTP_WS_MAX_MSG+1];
   int len = mg_snprintf(resp_buf, sizeof(resp_buf), "{ \"talk\": { \"cmd\": \"names\", \"ts\": %lu, \"users\": [", now);
   int count = 0;

   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      if (cptr->user == NULL) {
         continue;
      }

      len += mg_snprintf(resp_buf + len, sizeof(resp_buf) - len, "%s\"%s\"", count++ ? "," : "", cptr->user->name);
      cptr = cptr->next;
   }
   mg_snprintf(resp_buf + len, sizeof(resp_buf) - len, "] } }");
   struct mg_str ms = mg_str(resp_buf);
   ws_broadcast(NULL, &ms);

   return false;
}

void ws_blorp_userlist_cb(void *arg) {
   ws_send_userlist();
}

bool ws_kick_client(http_client_t *cptr, const char *reason) {
   char resp_buf[HTTP_WS_MAX_MSG+1];
   struct mg_connection *c = cptr->conn;

   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth.noisy", "ws_kick_client for cptr <%x> has mg_conn <%x> and is invalid", cptr, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   memset(resp_buf, 0, sizeof(resp_buf));
   snprintf(resp_buf, sizeof(resp_buf), "{ \"auth\": { \"error\": \"Disconnected: %s.\" } }", (reason != NULL ? reason : "No reason given."));
   mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);
   mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE);
   http_remove_client(c);

   return false;
}

bool ws_kick_client_by_c(struct mg_connection *c, const char *reason) {
   char resp_buf[HTTP_WS_MAX_MSG+1];

   if (c == NULL) {
      return true;
   }

   http_client_t *cptr = http_find_client_by_c(c);

   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth.noisy", "ws_kick_client_by_c for mg_conn <%x> has cptr <%x> and is invalid", c, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   return ws_kick_client(cptr, reason);
}

#define free_and_null(___x) do { if ((___x) != NULL) { free(___x); (___x) = NULL; } } while (0)

bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got msg <%x> c <%x> data <%x>", msg, c, (msg != NULL ? msg->data.buf : NULL));
      return true;
   }

//     Log(LOG_DEBUG, "http.noisy", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);

   struct mg_str msg_data = msg->data;

   // Handle different message types...
   if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
//      return ws_handle_cat_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
      return ws_handle_chat_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      return ws_handle_auth_msg(msg, c);
   }
   return false;
}
