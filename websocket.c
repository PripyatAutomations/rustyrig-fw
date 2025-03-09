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

// Send a broadcast message to all ws clients
void ws_broadcast(struct mg_connection *sender_conn, struct mg_str *msg_data) {
    struct ws_client *current = client_list;

    while (current != NULL) {
        if (current->conn != sender_conn) {
            mg_ws_send(current->conn, msg_data->buf, msg_data->len, WEBSOCKET_OP_TEXT);
        }
        current = current->next;
    }
}

// Add a WebSocket client to the client list
void ws_add_client(struct mg_connection *c) {
    // Allocate memory for a new client
    struct ws_client *new_client = (struct ws_client *)malloc(sizeof(struct ws_client));
    if (!new_client) {
        Log(LOG_WARN, "http.ws", "Failed to allocate memory for new WebSocket client");
        return;
    }

    new_client->conn = c;  // Set the connection
    new_client->next = client_list;  // Add it to the front of the list
    client_list = new_client;  // Update the head of the list
}

// Remove a WebSocket client from the client list
void ws_remove_client(struct mg_connection *c) {
    struct ws_client *prev = NULL;
    struct ws_client *current = client_list;

    // Traverse the list to find the client
    while (current != NULL) {
        if (current->conn == c) {
            // Found the client to remove
            if (prev == NULL) {
                // Removing the first element
                client_list = current->next;
            } else {
                // Removing a non-first element
                prev->next = current->next;
            }

            // Free the memory allocated for this client
            free(current);
            Log(LOG_DEBUG, "http.ws", "WebSocket client removed");
            return;
        }

        prev = current;
        current = current->next;
    }

    // If we reach here, the client was not found in the list
    Log(LOG_WARN, "http.ws", "Attempted to remove WebSocket client not in the list");
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
