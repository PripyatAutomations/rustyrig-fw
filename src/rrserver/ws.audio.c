//
// ws.audio.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include "../../ext/libmongoose/mongoose.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "rrserver/cat.h"
#include "rrserver/eeprom.h"
#include "rrserver/i2c.h"
#include "common/logger.h"
#include "common/posix.h"
#include "rrserver/state.h"
#include "rrserver/ws.h"

static bool is_in_array(const uint32_t *arr, size_t len, uint32_t id) {
   for (size_t i = 0; i < len; i++) {
      if (arr[i] == id) {
         return true;
      }
   }

   return false;
}
extern struct GlobalState rig;	// Global state

// Send to all users subscribed to this channel #
void au_send_to_ws(const void *data, size_t len, int channel) {
//   struct mg_str msg = mg_str_n((const char *)data, len);
//   ws_broadcast(NULL, &msg, WEBSOCKET_OP_BINARY);
   struct mg_connection *c;
   http_client_t *current = http_client_list;
   while (current) {
      if (current && (current->is_ws && current->authenticated)) {
         struct mg_connection *c = current->conn;

         if (c->is_websocket) {
            // Is this in the the array?
            if (is_in_array(current->rx_channels, MAX_RX_CHANNELS, channel) ||
                is_in_array(current->tx_channels, MAX_TX_CHANNELS, channel)) {
               mg_ws_send(c, data, len, WEBSOCKET_OP_BINARY);
            }
         }
      }
      current = current->next;
   }
}

// Find an existing channel
u_int32_t au_find_channel(const char codec[5], bool tx) {
   u_int32_t ret = 0;
   return ret;
}

// Create's a new channel
u_int32_t au_create_channel(const char codec[5], bool tx) {
   u_int32_t ret = 0;
   return ret;
}

// Find existing or create another channel
u_int32_t au_find_or_create_channel(const char codec[5], bool tx) {
   u_int32_t ret = au_find_channel(codec, tx);
   if (ret > 0) {
      return ret;
   }
   return au_create_channel(codec, tx);
}

// Send a subscribe message for a channel we found already
bool au_send_subscribe(u_int32_t channel) {
   return false;
}

// Send an unsubscribe message for a channel we found already
bool au_send_unsubscribe(u_int32_t channel) {
   return false;
}
