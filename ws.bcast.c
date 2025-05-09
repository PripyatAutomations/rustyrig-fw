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
