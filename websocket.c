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

// Example: In your WebSocket connection handler
bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
    struct mg_str msg_data = msg->data;

    // Handle different message types...
    if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
        // Handle cat-related messages
    } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
        // Handle talk-related messages
        Log(LOG_DEBUG, "chat.noisy", "Got message from client: |%.*s|", msg_data.len, msg_data.buf);
        ws_broadcast(c, &msg_data);  // Send to all connected clients
    }

    return false;
}
