//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Linux OpenSSL/wslay driver. Transport handles are borrowed until CLOSE;
 * the driver reclaims them after that callback returns. Abort is deferred so callers may reject a message safely from inside its callback. */
#ifndef RR_MESH_WSS_H
#define RR_MESH_WSS_H
#include <librrprotocol/transport.h>
typedef bool (*rr_mesh_pin_fn)(const uint8_t pin[32]);
typedef bool (*rr_mesh_http_fn)(const char *path, const char *body, size_t len, int *status, char **reply);
bool rr_mesh_wss_init(const char *cert, const char *key, rr_mesh_pin_fn trust);
void rr_mesh_wss_http_handler(rr_mesh_http_fn handler);
rr_transport_t *rr_mesh_wss_accept(int fd, rr_transport_callback_t cb, void *arg);
rr_transport_t *rr_mesh_wss_connect(const char *url, const char *pin, rr_transport_callback_t cb, void *arg);
void rr_mesh_wss_abort(rr_transport_t *t, const char *reason);
bool rr_mesh_wss_binding(rr_transport_t *t, uint8_t out[32]);
const char *rr_mesh_wss_ip(rr_transport_t *t);
unsigned rr_mesh_wss_peer_port(rr_transport_t *t);
const char *rr_mesh_wss_path(rr_transport_t *t);
const char *rr_mesh_wss_user_agent(rr_transport_t *t);
size_t rr_mesh_wss_queued(rr_transport_t *t);
void rr_mesh_wss_ping(rr_transport_t *t);
/* Pongs are delivered as EVENT_PONG with empty data for link health. */
void rr_mesh_wss_stop(void);
#endif
