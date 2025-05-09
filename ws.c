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

bool send_global_alert(http_client_t *cptr, const char *sender, const char *data) {
   if (cptr == NULL || data == NULL) {
      return true;
   }

   char msgbuf[HTTP_WS_MAX_MSG+1];
   struct mg_str mp;
   char *escaped_msg = escape_html(data);
   prepare_msg(msgbuf, sizeof(msgbuf), 
      "{ \"alert\": { \"from\": \"%s\", \"alert\": \"%s\", \"ts\": %lu } }",
      cptr->chatname, escaped_msg, now);
   mp = mg_str(msgbuf);
   ws_broadcast(NULL, &mp);
   free(escaped_msg);

   return false;
}

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
      if (current == NULL || !current->session_start) {
         break;
      }

      // NULL sender means it came from the server itself
      if ((sender == NULL) || (current->is_ws && current->conn != sender)) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
      }
      current = current->next;
   }
}

// Send to a specific, authenticated websocket session
void ws_send_to_cptr(struct mg_connection *sender, http_client_t *acptr, struct mg_str *msg_data) {
   if (sender == NULL || acptr == NULL || msg_data == NULL) {
      return;
   }

   mg_ws_send(acptr->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
}

// Send to all logged in instances of the user
void ws_send_to_name(struct mg_connection *sender, const char *username, struct mg_str *msg_data) {
   if (sender == NULL || username == NULL || msg_data == NULL) {
      Log(LOG_CRIT, "ws", "ws_send_to_name passed incomplete data; sender:<%x>, username:<%x>, msg_data:<%x>", sender, username, msg_data);
      return;
   }

   http_client_t *current = http_client_list;
   while (current != NULL) {
      if (current == NULL) {
         break;
      }

      // Messages from the server will have NULL sender
      if ((sender == NULL) || (current->is_ws && current->conn != sender)) {
         ws_send_to_cptr(sender, current, msg_data);
      }
   }
}

bool ws_send_userlist(void) {
   char resp_buf[HTTP_WS_MAX_MSG+1];
   int len = mg_snprintf(resp_buf, sizeof(resp_buf), 
       "{ \"talk\": { \"cmd\": \"names\", \"ts\": %lu, \"users\": [",
       now);
   int count = 0;

   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      if (cptr->user == NULL || !cptr->authenticated) {
         cptr = cptr->next;
         continue;
      }

      const char *comma = (count > 0) ? "," : "";

      bool is_admin = (strstr(cptr->user->privs, "admin") != NULL);
      bool is_view_only = (strstr(cptr->user->privs, "view") != NULL);
      bool is_txing = cptr->is_ptt;			// does user have PTT active on any rigs?
      bool is_owner = (strstr(cptr->user->privs, "owner") != NULL);

      len += mg_snprintf(resp_buf + len, sizeof(resp_buf) - len,
                   "%s{\"name\":\"%s\",\"admin\":%s,\"tx\":%s,\"view_only\":%s,\"owner\":%s}",
                   comma, cptr->chatname,
                   is_admin ? "true" : "false",
                   is_txing ? "true" : "false",
                   is_view_only ? "true" : "false",
                   is_owner ? "true" : "false");
      
      count++;
      cptr = cptr->next;
   }
   len += mg_snprintf(resp_buf + len, sizeof(resp_buf) - len, "] } }\n");

   struct mg_str ms = mg_str(resp_buf);
   ws_broadcast(NULL, &ms);

   return false;
}

// Wrap ws_send_userlist so it can be called by a timer
void ws_blorp_userlist_cb(void *arg) {
   ws_send_userlist();
}

bool ws_kick_client(http_client_t *cptr, const char *reason) {
   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth", "ws_kick_client for cptr <%x> has mg_conn <%x> and is invalid", cptr, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   if (cptr->user_agent != NULL) {
      free(cptr->user_agent);
      cptr->user_agent = NULL;
   }

   char resp_buf[HTTP_WS_MAX_MSG+1];
   struct mg_connection *c = cptr->conn;

   if (c == NULL) {
      return true;
   }

   prepare_msg(resp_buf, sizeof(resp_buf), 
      "{ \"auth\": { \"error\": \"Client kicked: %s\" } }",
      (reason != NULL ? reason : "no rason given"));
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
      Log(LOG_DEBUG, "auth", "ws_kick_client_by_c for mg_conn <%x> has cptr <%x> and is invalid", c, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   return ws_kick_client(cptr, reason);
}

static bool ws_handle_pong(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = false;
   char *ts = NULL;

   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong got msg <%x> c <%x> data <%x>", msg, c, (msg != NULL ? msg->data.buf : NULL));
      rv = true;
      goto cleanup;
   }

   char ip[INET6_ADDRSTRLEN];  // Buffer to hold IPv4 or IPv6 address
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   http_client_t *cptr = http_find_client_by_c(c);

   if (cptr == NULL) {
      char msgbuf[512];

      prepare_msg(msgbuf, sizeof(msgbuf),
         "Kicking client from %s:%d who has no cptr?!?!!?",
         ip, port);
      Log(LOG_AUDIT, "http.pong", msgbuf);
      ws_kick_client_by_c(c, msgbuf);
      rv = true;
      goto cleanup;
   }

   struct mg_str msg_data = msg->data;
   
   if ((ts = mg_json_get_str(msg_data, "$.pong.ts")) == NULL) {
      Log(LOG_WARN, "http.ws", "ws_handle_pong: PONG from user with no timestamp");
      rv = true;
      goto cleanup;
   } else {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong: PONG from user %s with ts:|%s|", cptr->chatname, ts);
   }

   char *endptr;
   errno = 0;
   time_t ts_t = strtoll(ts, &endptr, 10);

   if (errno == ERANGE || ts_t < 0 || ts_t > LONG_MAX || *endptr != '\0') {
      Log(LOG_WARN, "http.pong", "Got invalid ts |%s| from client <%x>", ts, c);
      rv = true;
      goto cleanup;
   }

   time_t ping_expiry = ts_t + HTTP_PING_TIME;
   if ((ping_expiry) < now) {
      Log(LOG_AUDIT, "http.pong", "Late ping for mg_conn:<%x> on cptr:<%x> from %s:%d ts: %lu + %lu (timeout) < now %lu", c, cptr, ip, port, ts_t, HTTP_PING_TIMEOUT, now);
      ws_kick_client(cptr, "Network Error: PING timeout");
      rv = true;
      goto cleanup;
   } else { // The pong response is valid, update the client's data
      cptr->last_heard = now;
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;
      Log(LOG_CRAZY, "http.pong", "Reset user %s last_heard to now:[%lu] and last_ping to 0", cptr->chatname, now);
   }

cleanup:
   free(ts);

   return rv;
}

bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got msg <%x> c <%x> data <%x>", msg, c, (msg != NULL ? msg->data.buf : NULL));
      return true;
   }

   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);

   if (msg->flags & WEBSOCKET_OP_BINARY) {
      codec_decode_frame((unsigned char *)msg->data.buf, msg->data.len);
   } else {	// Text based packet
      struct mg_str msg_data = msg->data;

      // Update the last-heard time for the user
      http_client_t *cptr = http_find_client_by_c(c);
      if (cptr != NULL) {
         cptr->last_heard = now;
      }

      // Handle different message types...
      if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
         return ws_handle_rigctl_msg(msg, c);
      } else if (mg_json_get(msg_data, "$.ping", NULL) > 0) {
         mg_ws_send(c, "pong", 4, WEBSOCKET_OP_TEXT);
      } else if (mg_json_get(msg_data, "$.pong", NULL) > 0) {
         return ws_handle_pong(msg, c);
      } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
         return ws_handle_chat_msg(msg, c);
      } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
         return ws_handle_auth_msg(msg, c);
      }
   }
   return false;
}

bool ws_send_error(struct mg_connection *c, const char *scope, const char *msg) {
   if (c == NULL || scope == NULL || msg == NULL) {
      return true;
   }

   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"%s\": { \"error\": \"%s\", \"ts\": %lu } }",
      scope, msg, now);

   return false;
}

void au_send_to_ws(const void *data, size_t len) {
   struct mg_str msg = mg_str_n((const char *)data, len);
   ws_broadcast(NULL, &msg);
}
