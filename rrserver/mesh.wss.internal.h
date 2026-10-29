#ifndef RR_MESH_WSS_INTERNAL_H
#define RR_MESH_WSS_INTERNAL_H
#include "mesh.wss.h"
#include <librustyaxe/reactor.glib.h>
#include <openssl/ssl.h>
#include <wslay/wslay.h>
#include <gio/gio.h>
#define MESH_QUEUE 262144
#define MESH_FRAME 32768
#define CLIENT_FRAME 65535
#define MESH_PROTOCOL "rustyrig-mesh.v1"
struct mesh_wss {
   struct mesh_wss *next;
   rr_transport_t *transport;
   SSL *ssl;
   wslay_event_context_ptr ws;
   rr_reactor_watch_t *watch;
   GCancellable *resolve_cancel;
   GList *addresses, *next_address;
   int fd;
   bool client, connecting, tls_done, open, dead, resolving, peer_verified, listed;
   bool http_reply, draining, client_route;
   int tls_want, read_want, write_want;
   uint8_t pin[32], binding[32], expected[32];
   char ip[64], host[256], authority[320], accept[32], reason[256], path[64];
   char user_agent[257];
   unsigned port, peer_port;
   long long started;
   long long close_started;
   /* Stable plaintext buffer: retry SSL_write with the same bytes/length. */
   uint8_t tx[MESH_QUEUE];
   size_t tx_len, retry_len, read_budget;
   char http[8192];
   size_t http_len;
};
bool rr_mesh_upgrade_prepare(struct mesh_wss *c);
/* 0 needs input, 1 complete, -1 rejected; consumes only HTTP header bytes. */
int rr_mesh_upgrade_parse(struct mesh_wss *c);
int rr_native_http_request(struct mesh_wss *c, rr_mesh_http_fn handler);
void rr_mesh_wss_arm(struct mesh_wss *c);
#endif
