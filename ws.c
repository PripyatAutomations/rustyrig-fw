//
// ws.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "inc/cat.h"
#include "inc/codec.h"
#include "inc/eeprom.h"
#include "inc/i2c.h"
#include "inc/logger.h"
#include "inc/posix.h"
#include "inc/state.h"
#include "inc/ws.h"

extern struct GlobalState rig;	// Global state

bool ws_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");
      return true;
   }

   Log(LOG_DEBUG, "http.ws", "WebSocket init completed succesfully");
   return false;
}

// Send to a specific, authenticated websocket session
void ws_send_to_cptr(struct mg_connection *sender, http_client_t *cptr, struct mg_str *msg_data) {
   if (cptr == NULL || msg_data == NULL) {
      return;
   }

   mg_ws_send(cptr->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
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

bool ws_kick_by_name(const char *name, const char *reason) {
   if (http_client_list == NULL) {
      return true;
   }

   http_client_t *curr = http_client_list;
   while (curr != NULL) {
      if (strcasecmp(name, curr->chatname) == 0) {
         ws_kick_client(curr, reason);
      }
      curr = curr->next;
   }
   return false;
}

bool ws_kick_by_uid(int uid, const char *reason) {
   if (http_client_list == NULL) {
      return true;
   }

   http_client_t *curr = http_client_list;
   while (curr != NULL) {
      if (uid == curr->user->uid) {
         ws_kick_client(curr, reason);
      }
      curr = curr->next;
   }
   return false;
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

   if (cptr->cli_version != NULL) {
      free(cptr->cli_version);
      cptr->cli_version = NULL;
   }

   char resp_buf[HTTP_WS_MAX_MSG+1];
   struct mg_connection *c = cptr->conn;

   if (c == NULL) {
      return true;
   }

   // make sure we're not accessing unsafe memory
   if (cptr->user != NULL && cptr->chatname[0] != '\0') {
      if (cptr->active) {
         // blorp out a quit to all connected users
         prepare_msg(resp_buf, sizeof(resp_buf),
                     "{ \"talk\": { \"cmd\": \"quit\", \"user\": \"%s\", \"reason\": \"%s\", \"ts\": %li } }",
                     cptr->chatname, reason, now);
         struct mg_str ms = mg_str(resp_buf);
         ws_broadcast(NULL, &ms);
      }
   }

   prepare_msg(resp_buf, sizeof(resp_buf), 
      "{ \"auth\": { \"error\": \"Client kicked: %s\" } }",
      (reason != NULL ? reason : "no reason given"));
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
      Log(LOG_AUDIT, "http.pong", "Late ping for mg_conn:<%x> on cptr:<%x> from %s:%d ts: %li + %li (timeout) < now %li", c, cptr, ip, port, ts_t, HTTP_PING_TIMEOUT, now);
      ws_kick_client(cptr, "Network Error: PING timeout");
      rv = true;
      goto cleanup;
   } else { // The pong response is valid, update the client's data
      cptr->last_heard = now;
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;
      Log(LOG_CRAZY, "http.pong", "Reset user %s last_heard to now:[%li] and last_ping to 0", cptr->chatname, now);
   }

cleanup:
   free(ts);
   return rv;
}


// Deal with the binary requests
static bool ws_binframe_process(const char *buf, size_t len) {
   Log(LOG_DEBUG, "ws.binframe", "Binary frame of %li bytes", len);

   if (buf[0] == 'u') {  // PCM-u
   } else if (buf[0] == 'O') {
#if	defined(FEATURE_OPUS)
       codec_decode_frame((unsigned char *)buf, len);
#endif
    }
    return false;
}

//
////
// This needs optimized!
///
static bool ws_txtframe_process(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data, "$.talk.cmd");
   char *data = mg_json_get_str(msg_data, "$.talk.data");
   char *target =  mg_json_get_str(msg_data, "$.talk.args.target");
   char *msg_type =  mg_json_get_str(msg_data, "$.type");
   char *hello = mg_json_get_str(msg_data, "$.hello");
   char *ping = mg_json_get_str(msg_data, "$.ping");

   bool result = false;

   // Update the last-heard time for the user
   http_client_t *cptr = http_find_client_by_c(c);
   if (cptr != NULL) {
      cptr->last_heard = now;
   }

   // Handle ping messages
   if (ping != NULL) {
      char ts_buf[32];
      char *ping_ts = mg_json_get_str(msg_data, "$.ping.ts");
      if (ping_ts != NULL) {
         snprintf(ts_buf, sizeof(ts_buf), "%s", ping_ts);

         char pong[128];
         snprintf(pong, sizeof(pong), "{\"type\":\"pong\",\"ts\":%s", ts_buf);
         mg_ws_send(c, pong, strlen(pong), WEBSOCKET_OP_TEXT);
         free(ping_ts);
      }
      goto cleanup;
   } else if (hello != NULL) {
      Log(LOG_DEBUG, "ws", "Got HELLO from client at c:<%x>: %s", c, hello);
      cptr->cli_version = malloc(HTTP_UA_LEN);
      memset(cptr->cli_version, 0, HTTP_UA_LEN);
      snprintf(cptr->cli_version, HTTP_UA_LEN, "%s", hello);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
      result = ws_handle_rigctl_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
      if (cmd != NULL) {
         result = ws_handle_chat_msg(msg, c);
      }
   } else if (mg_json_get(msg_data, "$.pong", NULL) > 0) {
      result = ws_handle_pong(msg, c);
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      result = ws_handle_auth_msg(msg, c);
   }

cleanup:
   free(cmd);
   free(data);
   free(target);
   free(msg_type);
   free(hello);
   free(ping);

   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got msg <%x> c <%x> data <%x>", msg, c, (msg != NULL ? msg->data.buf : NULL));
      return true;
   }

#if	defined(HTTP_DEBUG_CRAZY) || defined(DEBUG_PROTO)
   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
#endif

   // Binary (audio, waterfall) frames
   if (msg->flags & WEBSOCKET_OP_BINARY) {
      Log(LOG_DEBUG, "ws", "Binary frame: %li bytes", msg->data.len);
      ws_binframe_process(msg->data.buf, msg->data.len);
   } else {	// Text (mostly json) frames
//      Log(LOG_DEBUG, "ws", "Text frame: %li bytes", msg->data.len);
      ws_txtframe_process(msg, c);
   }
   return false;
}

bool ws_send_error_msg(struct mg_connection *c, const char *scope, const char *msg) {
   if (c == NULL || scope == NULL || msg == NULL) {
      return true;
   }

   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"%s\": { \"error\": \"%s\", \"ts\": %li } }",
      scope, msg, now);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   return false;
}

bool ws_send_error(http_client_t *cptr, const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   char tmpbuf[8192];
   memset(tmpbuf, 0, sizeof(tmpbuf));
   vsnprintf(tmpbuf, 8192, fmt, ap);

   char msgbuf[HTTP_WS_MAX_MSG+1];
   char *escaped_msg = escape_html(tmpbuf);
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"error\": \"%s\", \"ts\": %li }",
      tmpbuf, now);
   mg_ws_send(cptr->conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   va_end(ap);
   free(escaped_msg);
   return false;
}

bool ws_send_notice(struct mg_connection *c, const char *msg) {
   if (c == NULL || msg == NULL) {
      return true;
   }

   char *escaped_msg = escape_html(msg);
   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"notice\": \"%s\", \"ts\": %li }",
      escaped_msg, now);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   free(escaped_msg);
   return false;
}

bool ws_send_ping(http_client_t *cptr) {
   if (cptr == NULL || !cptr->is_ws) {
      return true;
   }

   // XXX: Send a ping, so they'll have something to respond to, to acknowledge life
   char resp_buf[HTTP_WS_MAX_MSG+1];
   struct mg_connection *c = cptr->conn;

   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth", "ws_send_ping for cptr:<%x> has mg_conn:<%x> and is invalid", cptr, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }

   // Make sure that timeout will happen if no response
   cptr->last_ping = now;
   cptr->ping_attempts++;

   // only bother making noise if the first attempt failed, send the first ping to crazy level log
   if (cptr->ping_attempts > 1) {
      Log(LOG_DEBUG, "ping", "sending ping to user %s on cptr:<%x> with ts:[%li] attempt %d",
          cptr->chatname, cptr, now, cptr->ping_attempts);
   } else {
      Log(LOG_CRAZY, "ping", "sending ping to user %s on cptr:<%x> with ts:[%li] attempt %d",
          cptr->chatname, cptr, now, cptr->ping_attempts);
   }

   prepare_msg(resp_buf, sizeof(resp_buf), "{ \"ping\": { \"ts\": %li } }", now);
   mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

   return false;
}
