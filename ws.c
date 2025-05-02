#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "cat.h"
#include "codec.h"
#include "eeprom.h"
#include "i2c.h"
#include "logger.h"
#include "posix.h"
#include "state.h"
#include "ws.h"

extern struct GlobalState rig;	// Global state

bool ws_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");
      return true;
   }

   // send the userlist to connected users every now and then
   mg_timer_add(mgr, CHAT_NAMES_INTERVAL, MG_TIMER_REPEAT, ws_blorp_userlist_cb, NULL);

   Log(LOG_DEBUG, "http.ws", "WebSocket init completed succesfully");
   return false;
}

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(struct mg_connection *sender, struct mg_str *msg_data) {
   if (msg_data == NULL) {
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
      if (cptr->user == NULL || !cptr->authenticated) {
         cptr = cptr->next;
         continue;
      }

      const char *comma = (count > 0) ? "," : "";
      const char *name_fmt = (strcasecmp(cptr->user->name, "guest") == 0) ? "%s%04d" : "%s";
      char username[64];
      snprintf(username, sizeof(username), name_fmt, cptr->user->name, cptr->guest_id);

      bool is_admin = (strstr(cptr->user->privs, "admin") != NULL);
      bool is_view_only = (strstr(cptr->user->privs, "view") != NULL);
      bool is_txing = cptr->is_ptt;			// does user have PTT active on any rigs?
      bool is_owner = (strstr(cptr->user->privs, "owner") != NULL);

      len += mg_snprintf(resp_buf + len, sizeof(resp_buf) - len,
                   "%s{\"name\":\"%s\",\"admin\":%s,\"tx\":%s,\"view_only\":%s,\"owner\":%s}",
                   comma, username,
                   is_admin ? "true" : "false",
                   is_txing ? "true" : "false",
                   is_view_only ? "true" : "false",
                   is_owner ? "true" : "false");
      
      count++;
      cptr = cptr->next;
   }
   len += mg_snprintf(resp_buf + len, sizeof(resp_buf) - len, "] } }");

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
   snprintf(resp_buf, sizeof(resp_buf), "{ \"auth\": { \"error\": \"%s.\" } }", (reason != NULL ? reason : "No reason given."));
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

static bool ws_handle_ping(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      return true;
   }
   struct mg_str msg_data = msg->data;
   char *ts = mg_json_get_str(msg_data, "$.ping.ts");
   if (ts == NULL) {
      free(ts);
      return true;
   }

   char *endptr;
   errno = 0;
   time_t ts_t = strtoll(ts, &endptr, 10);

   if (errno == ERANGE || ts_t < 0 || ts_t > LONG_MAX || *endptr != '\0') {
      Log(LOG_DEBUG, "http.ping", "Got invalid ts |%s| from client <%x>", ts, c);
      free(ts);
      return true;
   }
   free(ts);      

   if (ts_t + HTTP_PING_TIMEOUT < now) {
      Log(LOG_AUDIT, "http.ping", "Ping timeout for conn <%x>", c);
      // XXX: We need to kick the user here if its too late
   } else {
      // XXX: Find the cptr and cleear last_ping, update last_heard to now
   }

   return false;
}

bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got msg <%x> c <%x> data <%x>", msg, c, (msg != NULL ? msg->data.buf : NULL));
      return true;
   }

//     Log(LOG_DEBUG, "http.noisy", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
   if (msg->flags & WEBSOCKET_OP_BINARY) {
      codec_decode_frame((unsigned char *)msg->data.buf, msg->data.len);
   } else {	// Text based packet
      struct mg_str msg_data = msg->data;

      // Handle different message types...
      if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
         return ws_handle_rigctl_msg(msg, c);
      } else
       if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
         return ws_handle_chat_msg(msg, c);
      } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
         return ws_handle_auth_msg(msg, c);
      } else if (mg_json_get(msg_data, "$.ping", NULL) > 0) {
         return ws_handle_ping(msg, c);
      }
   }
   return false;
}

int generate_random_number(int digits) {
   int num = 0, prev_digit = -1;

try_again:
   for (int i = 0; i < digits; i++) {
      int digit;
      do {
         digit = rand() % 10;
      } while (digit == prev_digit); // Ensure no consecutive repeats

      num = num * 10 + digit;
      prev_digit = digit;
   }

   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      // if we match an existing number, start over
      if (cptr->guest_id == num) {
         goto try_again;
      }
      cptr = cptr->next;
   }
   return num;
}
