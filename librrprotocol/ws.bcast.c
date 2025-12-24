//
// ws.bcast.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
//#include "../ext/libmongoose/mongoose.h"
#include <librrprotocol/rrprotocol.h>

extern struct GlobalState rig;	// Global state
extern time_t now;

#if	defined(USE_MONGOOSE)
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
#endif	// defined(USE_MONGOOSE)


bool send_global_alert(const char *sender, const char *data) {
   if (!data) {
      return true;
   }

   const char *escaped_msg = escape_html(data);

   const char *jp = dict2json_mkstr(
      VAL_STR, "alert.from", sender,
      VAL_STR, "alert.msg", escaped_msg,
      VAL_LONG, "alert.ts", now);

#if	defined(USE_MONGOOSE)
   struct mg_str mp = mg_str(jp);
   ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
#endif
   free((char *)escaped_msg);
   free((char *)jp);

   return false;
}
