//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Authenticated server-to-server WS/WSS links form an undirected tree.
// Either endpoint may initiate a socket; overlapping components are rejected.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build_config.h"
#include <librustyaxe/core.h>
#include <rrserver/link.h>
#include <rrserver/acl.h>

#ifdef USE_MONGOOSE
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/link.wire.h>
#include <librrprotocol/server.events.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/link.tree.h>
#define MAX_PEERS 16
#define MAX_SESSIONS 32
#define MAX_QUEUE 262144

extern struct GlobalState rig;
struct link_session;

struct link_peer {
   char section[128], site[128], node[37];
   char *url, *password, *allowed_sites;
   bool autoconnect, leaf_only, policy_blocked;
   unsigned max_depth;
   long long retry_at;
   unsigned retry_seconds;
   bool manually_closed;
   struct link_session *session;
};

struct link_session {
   struct link_session *next;
   struct mg_connection *connection;
   struct link_peer *peer;
   long long started, last_ping, last_seen;
   unsigned generation;
   char node[37];
   bool outbound, dedicated, hello, snapshot, admitted, ready, established;
};

static struct {
   struct mg_mgr *manager;
   struct mg_connection *listener;
   struct link_session *sessions;
   struct link_peer peers[MAX_PEERS];
   size_t peer_count, session_count;
   rr_link_tree_t *tree;
   unsigned generation;
   char node[37], site[128];
   struct mg_str cert, key, ca;
   bool insecure, stopping;
} links;

static void link_callback(struct mg_connection *c, int event, void *data);

static const char *config_value(const char *key, const char *fallback) {
   const char *value = cfg_get(key);
   return value ? value : fallback;
}

static const char *peer_value(const char *section, const char *key) {
   static char name[256];
   snprintf(name, sizeof(name), "%s.%s", section, key);
   return cfg_get(name);
}

static bool equal_secret(const char *a, const char *b) {
   size_t an = a ? strlen(a) : 0, bn = b ? strlen(b) : 0;
   unsigned different = an != bn;

   for (size_t i = 0 ; i < 256 ; i++) {
      different |= (i < an ? (unsigned char)a[i] : 0) ^ (i < bn ? (unsigned char)b[i] : 0);
   }
   return !different;
}

static void fail(struct link_session *session, const char *reason) {
   Log(LOG_WARN, "link", "Connection to %s: %s",
      session->peer ? session->peer->site : "unknown station", reason);
   session->connection->is_closing = 1;
}

/* Depth is counted in edges beyond the configured direct peer: peer=0. */
static bool site_permitted(const struct link_peer *peer, const char *site) {
   if (!peer->allowed_sites || !*peer->allowed_sites) return true;
   char **names = g_strsplit_set(peer->allowed_sites, ", \t\r\n", -1);
   bool found = false;
   for (unsigned i = 0; names && names[i]; i++)
      if (*names[i] && !strcmp(names[i], site)) { found = true; break; }
   g_strfreev(names);
   return found;
}
static void topology_reject(struct link_session *session, const char *why) {
   if (session->peer) {
      session->peer->policy_blocked = true;
      Log(LOG_WARN, "link", "Peer %s blocked from automatic reconnect after topology violation; use /link open to retry", session->peer->site);
   }
   fail(session, why);
}
static bool snapshot_policy(const struct link_peer *peer, const rr_link_node_t *nodes, size_t count, const char *root) {
   if (peer->leaf_only && count != 1) return false;
   for (size_t i = 0; i < count; i++) {
      if (!strcmp(nodes[i].server, root)) continue;
      if (!site_permitted(peer, nodes[i].site)) return false;
      const char *parent = nodes[i].parent;
      unsigned depth = 0;
      while (parent && depth <= count) {
         depth++;
         if (!strcmp(parent, root)) break;
         const char *next = NULL;
         for (size_t j = 0; j < count; j++)
            if (!strcmp(nodes[j].server, parent)) { next = nodes[j].parent; break; }
         parent = next;
      }
      if (!parent || depth > count || (peer->max_depth && depth > peer->max_depth)) return false;
   }
   return true;
}
static bool introduction_policy(const struct link_session *session, const char *site, const char *parent) {
   if (session->peer->leaf_only || !site_permitted(session->peer, site)) return false;
   unsigned depth = 1;
   const char *current = parent;
   size_t limit = rr_link_tree_count(links.tree);
   while (current && strcmp(current, session->node) && depth <= limit) {
      rr_link_node_t node;
      bool found = false;
      for (size_t i = 0; i < limit; i++)
         if (rr_link_tree_node(links.tree, i, &node) && !strcmp(node.server, current)) {
            current = node.parent;
            found = true;
            break;
         }
      if (!found) return false;
      depth++;
   }
   return current && depth <= limit + 1 &&
      (!session->peer->max_depth || depth <= session->peer->max_depth);
}
static bool send_message(struct link_session *session, dict *d) {
   const char *op = dict_get(d, "op", "unknown");
   char *wire = rr_link_wire_encode(d);

   if (!wire) {
      char reason[256];
      snprintf(reason, sizeof(reason), "Cannot encode %.96s message (invalid fields or wire size limit); check link protocol compatibility", op);
      fail(session, reason);
      return false;
   }

   size_t length = strlen(wire);
   size_t queued = session->connection->send.len;

   if (session->connection->is_closing) {
      free(wire);
      return false;
   }

   if (queued > MAX_QUEUE || length > MAX_QUEUE - queued ||
       MAX_QUEUE - queued - length < 16) {
      char reason[256];
      snprintf(reason, sizeof(reason), "Cannot send %.96s: output queue full (%zu bytes queued, %zu-byte message, %d-byte limit)",
         op, queued, length, MAX_QUEUE);
      free(wire);
      fail(session, reason);
      return false;
   }

   mg_ws_send(session->connection, wire, length, WEBSOCKET_OP_TEXT);
   free(wire);
   return true;
}

static dict *message(const char *op) {
   dict *d = dict_new();

   if (d) {
      dict_add(d, "op", op);
   }
   return d;
}

const char *rrserver_links_node(void) {
   return links.node;
}
const char *rrserver_links_route(const char *node) {
   return rr_link_tree_route(links.tree, node);
}
bool rrserver_links_send(const char *node, dict *d) {
   const char *next = rrserver_links_route(node);

   if (!next) {
      return false;
   }

   for (struct link_session *s = links.sessions ; s ; s = s->next) {
      if (s->established && !strcmp(s->node, next)) {
         return send_message(s, d);
      }
   }
   return false;
}
void rrserver_links_broadcast(dict *d) {
   for (struct link_session *s = links.sessions ; s ; s = s->next) {
      if (s->established && !s->connection->is_closing) {
         send_message(s, d);
      }
   }
}

static void send_hello(struct link_session *session) {
   dict *d = message("link.hello");

   if (!d) {
      fail(session, "Out of memory");
      return;
   }
   dict_add(d, "node", links.node);
   dict_add(d, "site", links.site);
   dict_add(d, "password", session->peer->password);
   send_message(session, d);
   dict_free(d);
}

static void broadcast(struct link_session *exclude, dict *d) {
   for (struct link_session *s = links.sessions ; s ; s = s->next) {
      if (s != exclude && s->established && !s->connection->is_closing) {
         send_message(s, d);
      }
   }
}

/* Snapshot names are hex encoded because topology entries use spaces as separators. */
static void encode_site(const char *site, char *out) {
   static const char hex[] = "0123456789abcdef";
   while (*site) {
      unsigned char c = (unsigned char)*site++;
      *out++ = hex[c >> 4];
      *out++ = hex[c & 15];
   }
   *out = 0;
}

static bool decode_site(const char *hex, char *out, size_t capacity) {
   size_t n = strlen(hex);
   if (!n || (n & 1) || n / 2 >= capacity) return false;
   for (size_t i = 0 ; i < n ; i += 2) {
      unsigned char a = (unsigned char)hex[i], b = (unsigned char)hex[i + 1];
      int hi = a >= '0' && a <= '9' ? a - '0' : a >= 'a' && a <= 'f' ? a - 'a' + 10 : -1;
      int lo = b >= '0' && b <= '9' ? b - '0' : b >= 'a' && b <= 'f' ? b - 'a' + 10 : -1;
      if (hi < 0 || lo < 0 || !((hi << 4) | lo)) return false;
      out[i / 2] = (char)((hi << 4) | lo);
   }
   out[n / 2] = 0;
   return true;
}

static void send_snapshot(struct link_session *session) {
   size_t capacity = rr_link_tree_count(links.tree) * 332 + 1;
   char *nodes = calloc(1, capacity);

   if (!nodes) {
      fail(session, "Out of memory");
      return;
   }
   rr_link_node_t node;

   for (size_t i = 0 ; rr_link_tree_node(links.tree, i, &node) ; i++) {
      size_t n = strlen(nodes);
      char encoded[256];
       encode_site(node.site, encoded);
       int added = snprintf(nodes + n, capacity - n, "%s%s,%s,%s", i ? " " : "", node.server, node.parent ? node.parent : "-", encoded);

      if (added < 0 || (size_t)added >= capacity - n) {
         free(nodes);
         fail(session, "Topology snapshot too large");
         return;
      }
   }

   dict *d = message("link.snapshot");

   if (!d) {
      free(nodes);
      fail(session, "Out of memory");
      return;
   }
   dict_add(d, "nodes", nodes);
   session->generation = links.generation;
   send_message(session, d);
   dict_free(d);
   free(nodes);
}

static void announce_node(struct link_session *exclude, const char *id, const char *parent, const char *site) {
   dict *d = message("link.introduce");

   if (!d) {
      return;
   }
   dict_add(d, "node", id);
   dict_add(d, "parent", parent);
   dict_add(d, "site", site);
   broadcast(exclude, d);
   dict_free(d);
}

static void establish(struct link_session *session) {
   if (!session->snapshot || !session->ready || session->established) {
      return;
   }
   session->established = true;

   Log(LOG_INFO, "link", "Linked site %s (%s), servers %zu", session->peer->site, session->node, rr_link_tree_count(links.tree));
   /* Publish this component in parent-before-child order. */
   bool sent[256] = { false };

   for (size_t pass = 0 ; pass < rr_link_tree_count(links.tree) ; pass++) {
      rr_link_node_t node;

      for (size_t i = 1 ; rr_link_tree_node(links.tree, i, &node) ; i++) {
         const char *route = rr_link_tree_route(links.tree, node.server);

         if (sent[i] || !route || strcmp(route, session->node)) {
            continue;
         }
         const char *parent_route = rr_link_tree_route(links.tree, node.parent);
         bool parent_known = !parent_route || strcmp(parent_route, session->node);

         if (!parent_known) {
            rr_link_node_t parent;

            for (size_t j = 1 ; rr_link_tree_node(links.tree, j, &parent) ; j++) {
               if (!strcmp(parent.server, node.parent) && sent[j]) {
                  parent_known = true;
               }
            }
         }

         if (parent_known) {
            announce_node(session, node.server, node.parent, node.site);
            sent[i] = true;
         }
      }
   }
   event_emit("server.link.ready", NULL, session->node);
}

static void handle_hello(struct link_session *session, dict *d) {
   const char *site = dict_get(d, "site", ""), *node = dict_get(d, "node", "");

   if (session->hello || !rr_object_uuid_valid(node) || !strcmp(node, links.node) || strlen(site) >= 128 || !strcmp(site, links.site)) {
      fail(session, "Invalid station name or UUID in link hello");
      return;
   }
   struct link_peer *peer = session->peer;

   if (!peer) {
      for (size_t i = 0 ; i < links.peer_count ; i++) {
         if (!strcmp(links.peers[i].site, site)) {
            peer = &links.peers[i];
            break;
         }
      }
   }

   if (!peer || strcmp(peer->site, site) || (peer->node[0] && strcmp(peer->node, node)) || !equal_secret(peer->password, dict_get(d, "password", ""))) {
      fail(session, "Station name, UUID or shared password does not match the configured peer; check both servers' [peer] settings");
      return;
   }

   char peer_ip[64];
   mg_snprintf(peer_ip, sizeof(peer_ip), "%M", mg_print_ip, &session->connection->rem);
   const char *listener = session->connection->is_tls ? "listener:https" : "listener:http";

   if (rrserver_acl_check(peer->section, peer_ip, NULL, peer->site) != ACL_ALLOW ||
      (!session->outbound && rrserver_acl_check(session->dedicated ? "listener:link" : listener, peer_ip, NULL, peer->site) != ACL_ALLOW)) {
      fail(session, "Connection blocked by the access-control rules (ACL); check the peer and link-listener permissions");
      return;
   }

   if (peer->manually_closed) {
      fail(session, "Peer is administratively closed");
      return;
   }
   struct link_session *old = peer->session;

   if (old && old != session && !old->connection->is_closing) {
      if (old->established) {
         fail(session, "Peer already linked");
         return;
      }
      /* Only simultaneous attempts need a tie-break. Prefer the socket whose initiating server UUID is smaller, consistently at both endpoints. */
      bool prefer_outbound = strcmp(links.node, node) < 0;

      if (old->outbound == prefer_outbound || session->outbound != prefer_outbound) {
         fail(session, "Duplicate pending link");
         return;
      }
      fail(old, "Superseded simultaneous link");
   }

   /* Never export an uncommitted admission in another peer's snapshot. Serialize local admissions; established upstream deltas still invalidate the generation
    * check and cause a clean retry. */
   for (struct link_session *s = links.sessions ; s ; s = s->next) {
      if (s != session && !s->established && (s->admitted || (s->hello && !s->connection->is_closing))) {
         fail(session, "Another server is joining the network; this link will retry after the current join completes");
         return;
      }
   }

   peer->session = session;
   session->peer = peer;
   strcpy(session->node, node);
   session->hello = true;

   if (!session->outbound) {
      send_hello(session);
   }
   send_snapshot(session);
}

static void handle_snapshot(struct link_session *session, dict *d) {
   if (!session->hello || session->snapshot || session->generation != links.generation) {
      fail(session, "Server list changed during connection setup; reconnect to obtain an updated list");
      return;
   }
   char *copy = strdup(dict_get(d, "nodes", "")), *save = NULL;
   rr_link_node_t nodes[256];
   char sites[256][128];
   size_t count = 0;

   if (!copy) {
      fail(session, "Out of memory");
      return;
   }

   for (char *p = strtok_r(copy, " ", &save) ; p ; p = strtok_r(NULL, " ", &save)) {
      char *comma = strchr(p, ',');
      char *site = comma ? strchr(comma + 1, ',') : NULL;

      if (!comma || !site || count == 256) {
         count = 0;
         break;
      }
      *comma++ = 0;
      *site++ = 0;
      if (!decode_site(site, sites[count], sizeof(sites[count]))) {
         count = 0;
         break;
      }
      nodes[count] = (rr_link_node_t) {
         p, !strcmp(comma, "-") ? NULL : comma, sites[count]
      };
      count++;
   }

   bool named_peer = false;
   for (size_t i = 0 ; i < count ; i++) {
      if (!strcmp(nodes[i].server, session->node)) {
         named_peer = !strcmp(nodes[i].site, session->peer->site);
         break;
      }
   }
   if (named_peer && !snapshot_policy(session->peer, nodes, count, session->node)) {
      free(copy);
      topology_reject(session, "Peer topology violates leaf-only, allowed-sites or max-depth policy");
      return;
   }
   if (!named_peer || rr_link_tree_attach(links.tree, session->node, nodes, count) != RR_LINK_OK) {
      free(copy);
      fail(session, "Invalid server list (missing station name, duplicate server, routing loop or network size limit); check link protocol versions and topology");
      return;
   }
   free(copy);
   links.generation++;
   session->admitted = session->snapshot = true;
   dict *ready = message("link.ready");

   if (!ready) {
      fail(session, "Out of memory");
      return;
   }
   send_message(session, ready);
   dict_free(ready);
   establish(session);
}

static void handle_frame(struct link_session *session, struct mg_ws_message *frame) {
   if ((frame->flags & 15) != WEBSOCKET_OP_TEXT) {
      fail(session, "Only link control text is supported");
      return;
   }
   dict *d = rr_link_wire_decode(frame->data.buf, frame->data.len);

   if (!d) {
      Log(LOG_WARN, "link",
         "Rejected link protocol message from %s (%zu bytes); "
         "invalid format or incompatible protocol version",
         session->peer ? session->peer->site : "unknown server",
         frame->data.len);

      fail(session, "Invalid link protocol message");
      return;
   }
   const char *op = dict_get(d, "op", "");

   if (!strcmp(op, "link.hello")) {
      handle_hello(session, d);
   } else if (!strcmp(op, "link.snapshot")) {
      handle_snapshot(session, d);
   } else if (!strcmp(op, "link.ready") && session->hello && !session->ready) {
      if (!session->snapshot || links.generation != session->generation + 1) {
         fail(session, "Topology changed during admission; retry");
      } else {
         session->ready = true;
         establish(session);
      }
   } else if (session->established && !strcmp(op, "link.introduce")) {
      const char *node = dict_get(d, "node", ""), *parent = dict_get(d, "parent", "");
      const char *site = dict_get(d, "site", "");

      if (!introduction_policy(session, site, parent)) {
         topology_reject(session, "Introduced server violates leaf-only, allowed-sites or max-depth policy");
      } else if (rr_link_tree_add(links.tree, session->node, node, parent, site) != RR_LINK_OK) {
         fail(session, "Invalid server introduction (duplicate UUID, missing station name or incorrect network path)");
      } else {
         links.generation++;
         broadcast(session, d);
         Log(LOG_INFO, "link", "Introduced server %s (%s) via %s, servers %zu", site, node, session->peer->site, rr_link_tree_count(links.tree));
      }
   } else if (session->established && !strcmp(op, "link.withdraw")) {
      const char *node = dict_get(d, "node", "");

      if (!strcmp(node, session->node)) {
         fail(session, "Peer sent an invalid withdrawal of its own server; closing the link");
      } else if (!rr_link_tree_remove(links.tree, session->node, node)) {
         fail(session, "Server withdrawal does not belong to this peer's network branch");
      } else {
         links.generation++;
         broadcast(session, d);
         event_emit("server.link.lost", NULL, node);
      }
   } else if (session->established && !strcmp(op, "link.application")) {
      const char *origin = dict_get(d, "node", ""), *route = rrserver_links_route(origin);

      if (!route || strcmp(route, session->node)) {
         fail(session, "Message claims to originate from a server outside this peer's network branch");
      } else {
         event_emit_dict("server.link.application", NULL, d);
      }
   } else {
      fail(session, "Unexpected link operation");
   }
   dict_free(d);
}

static struct link_session *new_session(void) {
   if (links.session_count == MAX_SESSIONS) {
      return NULL;
   }
   struct link_session *session = calloc(1, sizeof(*session));

   if (session) {
      session->started = session->last_ping = session->last_seen = mono_ms();
      session->next = links.sessions;
      links.sessions = session;
      links.session_count++;
   }

   return session;
}

static void close_session(struct link_session *session) {
   if (session->admitted) {
      rr_link_tree_remove(links.tree, session->node, session->node);
      links.generation++;

      if (session->established) {
         dict *d = message("link.withdraw");

         if (d) {
            dict_add(d, "node", session->node);
            broadcast(session, d);
            dict_free(d);
         }
         Log(LOG_INFO, "link", "Unlinked site %s, servers %zu", session->peer->site, rr_link_tree_count(links.tree));
      }
   }

   if (session->peer && session->peer->session == session) {
      session->peer->session = NULL;
      session->peer->retry_at = mono_ms() + session->peer->retry_seconds * 1000LL;


   }
   event_emit("server.link.lost", NULL, session->node);
   struct link_session **p = &links.sessions;
   while (*p && *p != session) {
      p = &(*p)->next;
   }

   if (*p) {
      *p = session->next;
   }
   free(session);
   links.session_count--;
}

static void link_callback(struct mg_connection *c, int event, void *data) {
   if (c->is_listening) {
      return;
   }
   struct link_session *session = c->fn_data;

   if (event == MG_EV_ACCEPT) {
      session = new_session();

      if (!session) {
         c->is_closing = 1;
         return;
      }
      c->fn_data = session;
      session->connection = c;
      session->dedicated = true;
      char peer_ip[64];
      mg_snprintf(peer_ip, sizeof(peer_ip), "%M", mg_print_ip, &c->rem);

      if (rrserver_acl_check("listener:link", peer_ip, NULL, NULL) == ACL_DENY) {
         fail(session, "Listener ACL access denied");
         return;
      }

      if (links.cert.buf) {
         struct mg_tls_opts options = {
            .cert = links.cert, .key = links.key
         };
         mg_tls_init(c, &options);
      }
   }

   if (!session) {
      return;
   }
   session->connection = c;

   if (event == MG_EV_READ && c->recv.len > RR_LINK_MAX_FRAME + 4096) {
      fail(session, "Link receive buffer exceeded");
      return;
   }

   if (event == MG_EV_CONNECT && session->outbound) {
      char peer_ip[64];
      mg_snprintf(peer_ip, sizeof(peer_ip), "%M", mg_print_ip, &c->rem);

      if (rrserver_acl_check(session->peer->section, peer_ip, NULL, session->peer->site) != ACL_ALLOW) {
         fail(session, "Outbound connection blocked by this server's peer ACL");
         return;
      }
   }

   if (event == MG_EV_CONNECT && session->outbound && !strncmp(session->peer->url, "wss://", 6)) {
      struct mg_tls_opts options = {
         .ca = links.ca, .name = mg_url_host(session->peer->url)
      };
      mg_tls_init(c, &options);
   } else if (event == MG_EV_HTTP_MSG) {
      struct mg_http_message *hm = data;
      struct mg_str *protocol = mg_http_get_header(hm, "Sec-WebSocket-Protocol");

      if (mg_match(hm->uri, mg_str("/link"), NULL) && protocol && !mg_strcmp(*protocol, mg_str(RR_LINK_SUBPROTOCOL))) {
         /* Mongoose echoes the already-validated subprotocol once. */
         mg_ws_upgrade(c, hm, NULL);
      } else {
         mg_http_reply(c, 400, "", "Server-link protocol required\n");
         c->is_draining = 1;
      }
   } else if (event == MG_EV_WS_OPEN) {
      if (session->outbound) {
         struct mg_http_message *hm = data;
         struct mg_str *protocol = mg_http_get_header(hm, "Sec-WebSocket-Protocol");

         if (!protocol || mg_strcmp(*protocol, mg_str(RR_LINK_SUBPROTOCOL))) {
            fail(session, "Server did not negotiate link protocol");
            return;
         }
         send_hello(session);
      }
   } else if (event == MG_EV_WS_MSG) {
      session->last_seen = mono_ms();
      handle_frame(session, data);
   } else if (event == MG_EV_WS_CTL) {
      session->last_seen = mono_ms();
   } else if (event == MG_EV_ERROR) {
      struct mg_http_message response;
      char reason[256];

      /* Mongoose emits this error before consuming a rejected HTTP upgrade. Report only its status, never response headers or peer credentials. */
      if (session->outbound && !c->is_websocket && c->recv.buf &&
         mg_http_parse((const char *)c->recv.buf, c->recv.len, &response) > 0 &&
         response.method.len >= 5 && !memcmp(response.method.buf, "HTTP/", 5) && response.uri.len == 3) {
         snprintf(reason, sizeof(reason), "WebSocket upgrade rejected: HTTP %.*s; check /link URL and remote listener policy", (int)response.uri.len, response.
            uri.buf);
         fail(session, reason);
      } else {
         char detail[384];
         snprintf(detail, sizeof(detail), "Network connection to %s failed (%s); check URL, DNS, TLS certificate and remote server availability",
            session->peer && session->peer->url[0] ? session->peer->url : "incoming peer",
            data ? (const char *)data : "transport error");
         fail(session, detail);
      }
   } else if (event == MG_EV_CLOSE) {
      c->fn_data = NULL;
      close_session(session);
   }
}

static bool invalid_peer(const char *section, const char *key, const char *reason) {
   Log(LOG_CRIT, "link", "Invalid [%s] %s: %s", section, key, reason);
   return false;
}

static bool load_peer(const char *section) {
   const char *enabled = peer_value(section, "enabled");

   if (!enabled || !strcasecmp(enabled, "false")) {
      return true;
   }

   if (strcasecmp(enabled, "true")) {
      return invalid_peer(section, "enabled", "must be true or false");
   }

   if (links.peer_count == MAX_PEERS) {
      return invalid_peer(section, "enabled", "configured peer limit exceeded");
   }
   struct link_peer *peer = &links.peers[links.peer_count++];
   snprintf(peer->section, sizeof(peer->section), "%s", section);
   const char *site = peer_value(section, "site.name"), *node = peer_value(section, "node.uuid");

   if (!site || !*site || strlen(site) >= sizeof(peer->site)) {
      return invalid_peer(section, "site.name", "expected remote station name is required (1-127 characters)");
   }

   if (!strcmp(site, links.site)) {
      char reason[256];
      snprintf(reason, sizeof(reason), "must name the remote server; local station.name is %s", links.site);

      return invalid_peer(section, "site.name", reason);
   }
   strcpy(peer->site, site);

   if (node && *node) {
      if (!rr_object_uuid_valid(node)) {
         return invalid_peer(section, "node.uuid", "must be empty or a valid remote server UUID");
      }
      snprintf(peer->node, sizeof(peer->node), "%s", node);
   }

   for (size_t i = 0 ; i + 1 < links.peer_count ; i++) {
      if (!strcmp(links.peers[i].site, site)) {
         return invalid_peer(section, "site.name", "another enabled peer already uses this remote station name");
      }
   }

   const char *url = peer_value(section, "server.url"), *password = peer_value(section, "password"), *autoconnect = peer_value(section, "autoconnect");

   if (!password || !*password || strlen(password) > 256) {
      return invalid_peer(section, "password", "shared link password is required (1-256 characters)");
   }

   if (url && *url && strncmp(url, "wss://", 6) && !(links.insecure && !strncmp(url, "ws://", 5))) {
      return invalid_peer(section, "server.url", !strncmp(url, "ws://", 5) ?
         "ws:// requires links.allow-insecure=true in [general]" : "must use wss:// or explicitly enabled ws://");
   }
   peer->url = strdup(url ? url : "");
   peer->password = strdup(password);

   if (autoconnect && strcasecmp(autoconnect, "true") && strcasecmp(autoconnect, "false")) {
      return invalid_peer(section, "autoconnect", "must be true or false");
   }
   peer->autoconnect = autoconnect && !strcasecmp(autoconnect, "true");
   const char *leaf = peer_value(section, "leaf-only");
   if (leaf && strcasecmp(leaf, "true") && strcasecmp(leaf, "false"))
      return invalid_peer(section, "leaf-only", "must be true or false");
   peer->leaf_only = leaf && !strcasecmp(leaf, "true");
   const char *allowed = peer_value(section, "allowed-sites");
   peer->allowed_sites = strdup(allowed ? allowed : "");
   const char *max_text = peer_value(section, "max-depth");
   char *max_end = NULL;
   long max_depth = max_text && *max_text ? strtol(max_text, &max_end, 10) : 0;
   if (max_depth < 0 || max_depth > 255 || (max_end && *max_end))
      return invalid_peer(section, "max-depth", "must be 0-255 (0 is unlimited)");
   peer->max_depth = (unsigned)max_depth;
   const char *retry_text = peer_value(section, "retry-seconds");
   char *retry_end = NULL;
   long retry = retry_text && *retry_text ? strtol(retry_text, &retry_end, 10) : cfg_get_int("links.retry-seconds", 60);

   if (retry < 1 || retry > 3600 || (retry_end && *retry_end)) {
      return invalid_peer(section, "retry-seconds", "must be 1-3600 seconds");
   }
   peer->retry_seconds = (unsigned)retry;

   if (!peer->url || !peer->password || !peer->allowed_sites) {
      return invalid_peer(section, "credentials", "out of memory while loading peer");
   }
   return true;
}

/* Shared client HTTP/TLS listeners hand /link sockets to the peer protocol. TLS is already established here; peers never become authenticated clients. */
static void shared_link_request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_server_http_t)) {
      return;
   }
   rr_server_http_t *request = (rr_server_http_t *) data;

   if (!request->request || mg_strcmp(request->request->uri, mg_str("/link"))) {
      return;
   }
   request->handled = true;
   struct mg_connection *connection = request->connection;

   if (!connection->is_tls && !links.insecure) {
      mg_http_reply(connection, 403, "", "Plaintext server links are disabled\n");
      connection->is_draining = 1;
      return;
   }
   struct mg_str *protocol = mg_http_get_header(request->request, "Sec-WebSocket-Protocol");

   if (!protocol || mg_strcmp(*protocol, mg_str(RR_LINK_SUBPROTOCOL))) {
      mg_http_reply(connection, 400, "", "Server-link protocol required\n");
      connection->is_draining = 1;
      return;
   }
   struct link_session *session = new_session();

   if (!session) {
      mg_http_reply(connection, 503, "", "Server-link session limit reached\n");
      connection->is_draining = 1;
      return;
   }
   session->connection = connection;
   connection->fn = link_callback;
   connection->fn_data = session;
   request->detach_client = true;
   link_callback(connection, MG_EV_HTTP_MSG, request->request);
}

static void link_command(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!client || !client->authenticated || !client->user || !has_priv(client->user->uid, "admin|owner")) {
      return;
   }
   dict *request = data ? json2dict(data) : NULL;

   if (!request) {
      return;
   }
   char input[256], action[16], name[128], extra[2];
   snprintf(input, sizeof(input), "%s", dict_get(request, "talk.data", ""));
   dict_free(request);
   int argc = sscanf(input, "%15s %127s %1s", action, name, extra);

   if (argc <= 0 || (argc == 1 && !strcasecmp(action, "list"))) {
      for (size_t i = 0 ; i < links.peer_count ; i++) {
         struct link_peer *peer = &links.peers[i];
         ws_send_notice(client, "%s (%s): %s, retry=%us", peer->section, peer->site, peer->session
            && peer->session->established ? "linked" : peer->policy_blocked ? "topology blocked" : peer->manually_closed ? "closed" : "offline", peer->retry_seconds);
      }
      return;
   }

   if (argc != 2 || (strcasecmp(action, "open") && strcasecmp(action, "close"))) {
      ws_send_error(client, "Usage: /link list | open <stored-peer> | close <stored-peer>");
      return;
   }
   struct link_peer *peer = NULL;

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      struct link_peer *entry = &links.peers[i];
      const char *shortname = !strncmp(entry->section, "peer:", 5) ? entry->section + 5 : entry->section;

      if (!strcmp(name, entry->section) || !strcmp(name, shortname)) {
         peer = entry;
      }
   }

   if (!peer) {
      ws_send_error(client, "Unknown stored peer");
      return;
   }
   bool open = !strcasecmp(action, "open");

   if (open && !peer->url[0]) {
      ws_send_error(client, "Peer has no outgoing URL");
      return;
   }
   peer->manually_closed = !open;

   if (open) {
      peer->policy_blocked = false;
      peer->autoconnect = true;
      peer->retry_at = 0;
   } else if (peer->session) {
      fail(peer->session, "Closed by administrator");
   }
   Log(LOG_AUDIT, "link", "%s requested %s for %s", client->user->name, action, peer->section);
   ws_send_notice(client, "Peer %s: %s requested", peer->section, action);
}

bool rrserver_links_init(struct mg_mgr *manager) {
   memset(&links, 0, sizeof(links));
   links.manager = manager;
   const char *node = rr_rig_registry_node(rig.rigs);

   if (!node) {
      return false;
   }
   snprintf(links.node, sizeof(links.node), "%s", node);
   snprintf(links.site, sizeof(links.site), "%s", config_value("station.name", "rustyrig"));
   links.insecure = cfg_get_bool("links.allow-insecure", false);
   /* Discover [peer] and [peer:name] sections without duplicating config state. */
   int rank = 0;
   const char *key;
   char *value;
   char sections[MAX_PEERS][128];
   size_t count = 0;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (strncmp(key, "peer.", 5) && strncmp(key, "peer:", 5)) {
         continue;
      }
      const char *end = strchr(key, '.');

      if (!end || (size_t)(end - key) >= 128) {
         continue;
      }
      char section[128];
      memcpy(section, key, end - key);
      section[end - key] = 0;
      /* Default NULL template entries don't enable an unconfigured peer. */

      bool exists = false;

      for (size_t i = 0 ; i < count ; i++) {
         if (!strcmp(sections[i], section)) {
            exists = true;
         }
      }

      if (exists) {
         continue;
      }

      if (count == MAX_PEERS) {
         return false;
      }
      strcpy(sections[count++], section);
   }

   for (size_t i = 0 ; i < count ; i++) {
      if (!load_peer(sections[i])) {
         Log(LOG_CRIT, "link", "Invalid peer configuration in [%s]", sections[i]);
         return false;
      }
   }

   const char *listen = cfg_get("links.listen");

   if (!links.peer_count && (!listen || !*listen)) {
      event_on("link.cmd", link_command, NULL);
      return true;
   }
   int retry = cfg_get_int("links.retry-seconds", 60), timeout = cfg_get_int("links.handshake-seconds", 10);

   if (retry < 1 || retry > 3600 || timeout < 1 || timeout > 120) {
      return false;
   }
   links.tree = rr_link_tree_new(node, cfg_get_int("links.max-servers", 128));

   if (links.tree && !rr_link_tree_set_local_site(links.tree, links.site)) {
      Log(LOG_CRIT, "link", "Invalid station.name: expected 1-127 printable characters");
      rr_link_tree_free(links.tree);
      links.tree = NULL;
   }

   if (!links.tree) {
      return false;
   }

   if (listen && *listen) {
      bool tls = !strncmp(listen, "https://", 8);

      if (!tls && !(links.insecure && !strncmp(listen, "http://", 7))) {
         return false;
      }

      if (tls) {
         links.cert = mg_file_read(&mg_fs_posix, config_value("links.tls-cert", ""));
         links.key = mg_file_read(&mg_fs_posix, config_value("links.tls-key", ""));

         if (!links.cert.len || !links.key.len) {
            return false;
         }
      }
      links.listener = mg_http_listen(manager, listen, link_callback, NULL);

      if (!links.listener) {
         return false;
      }
   }
   bool needs_ca = false;

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      if (!strncmp(links.peers[i].url, "wss://", 6)) {
         needs_ca = true;
      }
   }

   if (needs_ca) {
      links.ca = mg_file_read(&mg_fs_posix, config_value("links.tls-ca", "/etc/ssl/certs/ca-certificates.crt"));

      if (!links.ca.len) {
         return false;
      }
   }

   if (cfg_get_bool("links.shared-listener", true)) {
      event_on_binary(RR_SERVER_HTTP_EVENT, shared_link_request, NULL);
   }

   if (links.peer_count || links.listener) {
      Log(LOG_INFO, "link", "Server link node %s (%s), %zu configured peers", links.node, links.site, links.peer_count);
   }

   event_on("link.cmd", link_command, NULL);
   return true;
}

void rrserver_links_poll(void) {
   if (!links.tree || links.stopping) {
      return;
   }
   long long at = mono_ms();

   for (struct link_session *s = links.sessions ; s ; s = s->next) {
      if (!s->established && at - s->started > cfg_get_int("links.handshake-seconds", 10) * 1000LL) {
         fail(s, "Connection setup timed out; check network reachability, TLS and peer configuration");
      }

      if (s->established && at - s->last_seen > 30000) {
         fail(s, "No response from linked server for 30 seconds; check network connectivity");
      }

      if (s->established && at - s->last_ping > 10000) {
         mg_ws_send(s->connection, "", 0, WEBSOCKET_OP_PING);
         s->last_ping = at;
      }
   }

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      struct link_peer *peer = &links.peers[i];

      if (peer->manually_closed || peer->policy_blocked || !peer->autoconnect || !peer->url[0] || peer->session || at < peer->retry_at) {
         continue;
      }

      struct link_session *s = new_session();

      if (!s) {
         continue;
      }
      s->peer = peer;
      s->outbound = true;
      peer->session = s;
      struct mg_connection *c = mg_ws_connect(links.manager, peer->url, link_callback, s, "Sec-WebSocket-Protocol: " RR_LINK_SUBPROTOCOL "\r\n");

      if (!c) {
         close_session(s);
      } else {
         s->connection = c;
      }
   }
}

void rrserver_links_fini(void) {
   links.stopping = true;
   event_off("link.cmd", link_command, NULL);
   event_off_binary(RR_SERVER_HTTP_EVENT, shared_link_request, NULL);
   /* Call after mg_mgr_free: CLOSE callbacks own session cleanup. */
   rr_link_tree_free(links.tree);
   links.tree = NULL;

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      free(links.peers[i].url);
      free(links.peers[i].password);
   }

   free((void *)links.cert.buf);
   free((void *)links.key.buf);
   free((void *)links.ca.buf);
}
#else
const char *rrserver_links_node(void) {
   return "";
}

const char *rrserver_links_route(const char *node) {
   return NULL;
}

bool rrserver_links_send(const char *node, dict *d) {
   return false;
}

void rrserver_links_broadcast(dict *d) {
}

bool rrserver_links_init(struct mg_mgr *manager) {
   return true;
}

void rrserver_links_poll(void) {
}

void rrserver_links_fini(void) {
}
#endif
