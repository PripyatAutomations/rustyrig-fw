// Owner-directed application relay and demand-based branch subscriptions.
#ifndef RR_SERVER_RELAY_H
#define RR_SERVER_RELAY_H
#include <librustyaxe/core.h>
void rrserver_relay_register_events(void);
void rrserver_relay_poll(void);
void rrserver_relay_fini(void);
void rrserver_relay_inventory(rrconn_t *client, const char *request);
void rrserver_relay_objects(rrconn_t *client, const char *request);
char *rrserver_relay_remote_rooms(void);
char *rrserver_relay_servers(void);
char *rrserver_relay_rooms(const char *local);
#endif
