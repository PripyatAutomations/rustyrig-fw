//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#ifndef RRSERVER_RELAY_INTERNAL_H
#define RRSERVER_RELAY_INTERNAL_H
#include <rrserver/relay.h>
#ifdef USE_MONGOOSE
#include <librrprotocol/ws.mediachan.h>

/* Relay-private state: owned and accessed on the server event-loop thread. */
#define MAX_CATALOG 2048
#define MAX_RELAY_SESSIONS 128
#define MAX_SUBSCRIPTIONS 512
struct catalog {
   struct catalog *next;
   char owner[37], key[160];
   dict *descriptor;
   bool seen;
};
struct wanted_room {
   struct wanted_room *next;
   char owner[37], room[128];
   bool pending;
};
struct relay_client {
   struct relay_client *next;
   rrconn_t *client;
   char id[37];
   struct wanted_room *wanted;
   long long retry_at;
};
struct remote_session {
   struct remote_session *next;
   char origin[37], id[37];
   rrconn_t client;
   uint64_t release_retry, proxy_started, proxy_frame;
   bool proxy_tx[MAX_MEDIA_CHANNELS];
};
struct subscription {
   struct subscription *next;
   char owner[37], room[128], node[37];
};
struct directory {
   struct directory *next;
   char node[37];
   dict *users;
};

/* Private channel discovery and stream translation are scoped to one client session. */
#define MAX_RELAY_MEDIA 512
#define RELAY_MEDIA_CHUNK 4000
struct relay_media {
   struct relay_media *next;
   rrconn_t *client;
   char owner[37], uuid[64], room[128];
   uint8_t stream, owner_stream, direction, subsystem;
   bool subscribed, setup, home;
   uint8_t *frame;
   size_t size, received;
   uint32_t sequence;
   uint64_t started;
};

extern struct directory *rr_relay_directories;
extern struct catalog *rr_relay_catalogs;
extern struct relay_client *rr_relay_clients;
extern struct remote_session *rr_relay_remotes;
extern struct subscription *rr_relay_subscriptions;
extern size_t rr_relay_catalog_count;
extern size_t rr_relay_client_count;
extern size_t rr_relay_remote_count;
extern size_t rr_relay_subscription_count;
extern size_t rr_relay_wanted_count;
extern bool rr_relay_applying;
extern bool rr_relay_catalog_dirty;
extern struct relay_media *rr_relay_media_maps;

/* User snapshots are invalidated by events, with a five-second safety sweep. */
void rr_relay_users_poll(bool force);
void rr_relay_users_reset(void);
void rr_relay_users_register(void);

/* Private cross-domain operations; public callers use relay.h. */
void rr_relay_directory_put(const char *node, dict *d);
dict *rr_relay_local_users(void);
void rr_relay_send_users(rrconn_t *client, const char *room);
bool rr_relay_local_owner(const char *node);
struct catalog *rr_relay_find_catalog(const char *key);
struct catalog *rr_relay_room_catalog(const char *room);
const char *rr_relay_message_room(dict *d);
void rr_relay_catalog_put(const char *owner, dict *d);
void rr_relay_collect_local(void);
void rr_relay_send_catalogs(const char *dest);
void rr_relay_dirty(const char *event, const char *data, rrconn_t *client, void *user);
void rr_relay_refresh_catalog(void);
struct relay_media *rr_relay_media_map(rrconn_t *client, const char *uuid, bool create);
void rr_relay_media_forget(rrconn_t *client, const char *owner, const char *room);
bool rr_relay_media_reassemble(struct relay_media *m, dict *d);
void rr_relay_media_outgoing(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_media_incoming(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_remote_talker(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_media_catalog(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_media_demand(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_media_fanout(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
bool rr_relay_proxy_tx_subscribed(struct remote_session *r, const char *room);
void rr_relay_proxy_tx_frame(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
struct remote_session *rr_relay_remote_for(rrconn_t *client);
struct relay_client *rr_relay_client_for(rrconn_t *client, bool create);
void rr_relay_wanted_set(struct relay_client *client, const char *owner, const char *room, bool joined);
void rr_relay_wanted_free(struct relay_client *client);
void rr_relay_rejoin_owned_room(const char *owner, const char *room);
bool rr_relay_demand(const char *owner, const char *room);
void rr_relay_subscribe_upstream(const char *owner, const char *room, bool subscribe);
bool rr_relay_interested(const char *joined, const char *room, dict *d);
bool rr_relay_client_interested(rrconn_t *client, const char *room, dict *d);
bool rr_relay_state_demand(const char *owner, const char *room, dict *d);
void rr_relay_publish_state(const char *owner, const char *room, dict *d);
bool rr_relay_remove_remote(struct remote_session **link);
void rr_relay_send_remote(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_publish(const char *event, const void *data, size_t length, rrconn_t *client, void *user);
void rr_relay_roster(const char *event, const char *room, rrconn_t *client, void *user);
void rr_relay_execute(const char *origin, const char *id, const char *username, dict *d);
void rr_relay_update_membership(rrconn_t *client, const char *room, bool joined);
void rr_relay_deliver(rrconn_t *client, dict *d);
bool rr_relay_packet(const char *dest, const char *kind, const char *session, const char *username, dict *payload);
void rr_relay_send_owned(const char *dest, const char *owner, const char *kind, dict *payload);
bool rr_relay_permitted_message(dict *d);
#endif /* USE_MONGOOSE */
#endif
