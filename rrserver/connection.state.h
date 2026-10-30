// Server-side client state predicates. Keep application demand independent of
// whether the connection uses Mongoose's WebSocket adapter or a dedicated
// transport implementation.
#ifndef RR_SERVER_CONNECTION_STATE_H
#define RR_SERVER_CONNECTION_STATE_H

#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <librrprotocol/rrprotocol.h>

/* Authenticated WebSocket sessions without an adopted adapter remain valid.
 * If an adapter is present, it must not be closed or closing. */
static inline bool rrserver_client_is_connected(const rrconn_t *client) {
   return client && client->is_ws && client->authenticated &&
      (!client->native_transport ||
       rr_transport_state(client->native_transport) == RR_TRANSPORT_OPEN);
}

/* Pure transport check: valid for relays and server links too, which do
 * not necessarily have the same end-user authentication/WS flags. */
static inline bool rrserver_transport_can_send_binary(const rrconn_t *client) {
   return client && client->native_transport &&
      rr_transport_state(client->native_transport) == RR_TRANSPORT_OPEN;
}

/* Authenticated end-user binary delivery. */
static inline bool rrserver_client_can_send_binary(const rrconn_t *client) {
   return rrserver_client_is_connected(client) &&
      rrserver_transport_can_send_binary(client);
}

#endif
