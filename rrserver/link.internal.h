//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Component-private session state, shared by admission and native adapter. */
#ifndef RR_LINK_INTERNAL_H
#define RR_LINK_INTERNAL_H
#include <glib.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/link.tree.h>
#include <rrserver/link.mesh.h>
#include <rrserver/globalstate.h>
#include "mesh.auth.h"
#define MAX_PEERS 16
#define MAX_SESSIONS 32
#define MAX_QUEUE 262144

extern struct GlobalState rig;
struct link_session;

struct link_peer {
   char section[128], site[128], node[37];
   char *url, *password, *allowed_sites, *catalog_key;
   char spki[45];
   bool enrolled;
   bool autoconnect, leaf_only, policy_blocked, startup_override;
   unsigned max_depth;
   long long retry_at;
   unsigned retry_seconds;
   unsigned clock_tolerance_seconds;
   long long clock_offset_seconds;
   bool clock_offset_valid;
   unsigned health_flaps, health_auth_errors;
   long long health_window_ms, health_rtt_ms;
   long long rtt_ring[30];
   unsigned rtt_next, rtt_count, rtt_high_streak;
   bool rtt_degraded;
   unsigned rtt_recovery_streak;
   bool manually_closed;
   struct link_session *session;
};

struct link_session {
   struct link_session *next;
   rr_transport_t *transport;
   struct rr_mesh_auth auth;
   bool closing;
   struct link_peer *peer;
   long long started, established_at, last_ping, last_seen, ping_sent;
   char close_reason[256];
   const char *close_cause;
   bool ping_pending;
   unsigned generation;
   char node[37];
   bool outbound, dedicated, hello, snapshot, admitted, ready, established;
   bool identity_sent, identity_seen;
};

struct rr_links {
   struct link_session *sessions;
   struct link_peer peers[MAX_PEERS];
   size_t peer_count, session_count;
   rr_link_tree_t *tree;
   rr_mesh_t *mesh;             /* Shadow topology: authenticated direct peers only. */
   unsigned generation;
   long long autoconnect_after;
   char node[37], domain[254], site[128];
   bool stopping;
};
extern struct rr_links links;


static inline bool session_closing(const struct link_session *s) {
   return s->closing;
}
static inline bool session_tls(const struct link_session *s) {
   return s->transport && (rr_transport_capabilities(s->transport) & RR_TRANSPORT_TLS_AUTHENTICATED);
}

struct link_session *new_session(void);
void close_session(struct link_session *session);
void fail_cause(struct link_session *session, const char *cause, const char *reason);
void fail(struct link_session *session, const char *reason);
void send_hello(struct link_session *session);
void handle_frame(struct link_session *session, const char *data, size_t length);
void handle_pong(struct link_session *session);
void native_callback(rr_transport_t *, enum rr_transport_event, rr_transport_channel_t *, const uint8_t *, size_t, void *);
void link_command(const char *, const char *, rrconn_t *, void *);
bool native_identity_trusted(const char *node, const char *key, const char *pin);
#endif
