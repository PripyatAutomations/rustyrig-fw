// Client-side session and transport readiness.
#ifndef RR_CLIENT_CONNECTION_STATE_H
#define RR_CLIENT_CONNECTION_STATE_H
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <librrprotocol/rrprotocol.h>

/* Application session readiness is not the same as having a binary adapter. */
static inline bool rrclient_connection_is_connected(const rrconn_t *c) {
   return c && c->active && c->connected && c->is_ws && c->authenticated &&
      (!c->native_transport || rr_transport_state(c->native_transport) == RR_TRANSPORT_OPEN);
}

static inline bool rrclient_connection_can_send_binary(const rrconn_t *c) {
   return rrclient_connection_is_connected(c) && c->native_transport &&
      rr_transport_state(c->native_transport) == RR_TRANSPORT_OPEN;
}
#endif
