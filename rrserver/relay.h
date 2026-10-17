//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Owner-directed application relay and demand-based branch subscriptions.
#ifndef RR_SERVER_RELAY_H
#define RR_SERVER_RELAY_H
#include <librustyaxe/core.h>
bool rrserver_relay_proxy_tx_active(rrconn_t *client, const char *room, uint64_t at);
bool rrserver_relay_media_valid(rrconn_t *client, const char *uuid, unsigned stream);
/* Borrowed user home-site name (UUID fallback for an unknown remote catalog). */
const char *rrserver_relay_client_server(const rrconn_t *client);
void rrserver_relay_register_events(void);
void rrserver_relay_visit_sessions(void (*visit) (rrconn_t *, void *), void *data);
void rrserver_relay_poll(void);
void rrserver_relay_fini(void);
void rrserver_relay_inventory(rrconn_t *client, const char *request);
void rrserver_relay_file_nodes(rrconn_t *client, const char *request_id);
void rrserver_relay_objects(rrconn_t *client, const char *request);
char *rrserver_relay_remote_rooms(void);
char *rrserver_relay_servers(void);
char *rrserver_relay_rooms(const char *local);
#endif
