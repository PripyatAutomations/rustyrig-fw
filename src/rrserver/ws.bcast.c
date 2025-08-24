//
// ws.bcast.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "../../ext/libmongoose/mongoose.h"
#include "common/cat.h"
#include "rrserver/eeprom.h"
#include "rrserver/i2c.h"
#include "common/logger.h"
#include "common/posix.h"
#include "rrserver/state.h"
#include "rrserver/ws.h"
#include "common/client-flags.h"

extern struct GlobalState rig;	// Global state

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(struct mg_connection *sender, struct mg_str *msg_data, int data_type) {
   if (!msg_data) {
      return;
   }

   http_client_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ((current->is_ws && current->authenticated) && (current->conn != sender)) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
      }
      current = current->next;
   }
}

// Broadcast a message to all WebSocket clients with matching flags (using http_client_list)
void ws_broadcast_with_flags(u_int32_t flags, struct mg_connection *sender, struct mg_str *msg_data, int data_type) {
   if (!msg_data) {
      return;
   }

   http_client_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if (current && (current->is_ws && current->authenticated) && (current->conn != sender)) {
         if (client_has_flag(current, flags)) {
            mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
         }
      }
      current = current->next;
   }
}

void ws_broadcast_audio(struct mg_connection *sender, struct mg_str *msg_data, int data_type, u_int32_t channel) {
   if (!msg_data) {
      return;
   }

   http_client_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ((current->is_ws && current->authenticated) && (current->conn != sender)) {
         // XXX: Compare the connection's codec 
//         if (current->rx_codecs[
//         mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
      }
      current = current->next;
   }
}

bool send_global_alert(const char *sender, const char *data) {
   if (!data) {
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
