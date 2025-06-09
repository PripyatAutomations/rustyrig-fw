//
// ws.bcast.c
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

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(struct mg_connection *sender, struct mg_str *msg_data, int data_type) {
   if (msg_data == NULL) {
      return;
   }

   http_client_t *current = http_client_list;
   while (current != NULL) {
      // NULL sender means it came from the server itself
      if ((current->is_ws && current->authenticated) && (current->conn != sender)) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
      }
      current = current->next;
   }
}

// Broadcast a message to all WebSocket clients with matching flags (using http_client_list)
void ws_broadcast_with_flags(u_int32_t flags, struct mg_connection *sender, struct mg_str *msg_data, int data_type) {
   if (msg_data == NULL) {
      return;
   }

   http_client_t *current = http_client_list;
   while (current != NULL) {
      // NULL sender means it came from the server itself
      if (current && (current->is_ws && current->authenticated) && (current->conn != sender)) {
         if (client_has_flag(current, flags)) {
            mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
         }
      }
      current = current->next;
   }
}

bool send_global_alert(const char *sender, const char *data) {
   if (data == NULL) {
      return true;
   }

   char msgbuf[HTTP_WS_MAX_MSG+1];
   struct mg_str mp;
   char *escaped_msg = escape_html(data);
   prepare_msg(msgbuf, sizeof(msgbuf), 
      "{ \"alert\": { \"from\": \"%s\", \"msg\": \"%s\", \"ts\": %lu } }",
      sender, escaped_msg, now);
   mp = mg_str(msgbuf);
   ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
   free(escaped_msg);

   return false;
}

// XXX: This needs to go away and just be sent through the ws_broadcast_with_flags()
void broadcast_audio_to_ws_clients(const void *data, size_t len) {
   struct mg_connection *c;
   http_client_t *current = http_client_list;
   while (current != NULL) {
      if (current && (current->is_ws && current->authenticated)) {
         struct mg_connection *c = current->conn;

         if (c->is_websocket) {
            mg_ws_send(c, data, len, WEBSOCKET_OP_BINARY);
         }
      }
      current = current->next;
   }
}
