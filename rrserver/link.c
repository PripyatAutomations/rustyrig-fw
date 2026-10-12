// Authenticated server-to-server WS/WSS links. Configuration defines a tree:
// one parent, any number of children; either endpoint may initiate the socket.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build_config.h"
#include <librustyaxe/core.h>
#include <rrserver/link.h>

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
   char *url, *username, *password;
   bool parent, autoconnect;
   long long retry_at;
   struct link_session *session;
};
struct link_session {
   struct link_session *next;
   struct mg_connection *connection;
   struct link_peer *peer;
   long long started, last_ping, last_seen;
   unsigned generation;
   char node[37], root[37];
   bool outbound, hello, snapshot, admitted, ready, established;
};
static struct {
   struct mg_mgr *manager;
   struct mg_connection *listener;
   struct link_session *sessions;
   struct link_peer peers[MAX_PEERS];
   size_t peer_count, session_count;
   rr_link_tree_t *tree;
   unsigned generation;
   char node[37], site[128], root[37];
   struct mg_str cert, key, ca;
   bool has_parent, insecure, stopping;
} links;

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
   Log(LOG_WARN, "link", "Peer %s: %s", session->peer ? session->peer->site : "(unauthenticated)", reason);
   session->connection->is_closing = 1;
}

static bool send_message(struct link_session *session, dict *d) {
   char *wire = rr_link_wire_encode(d);
   bool ok = wire && !session->connection->is_closing && session->connection->send.len + strlen(wire) + 16 <= MAX_QUEUE;

   if (ok) {
      mg_ws_send(session->connection, wire, strlen(wire), WEBSOCKET_OP_TEXT);
   }
   free(wire);

   if (!ok) {
      fail(session, "Link send queue or wire limit exceeded");
   }

   return ok;
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
   dict_add(d, "root", links.root);
   dict_add(d, "username", session->peer->username);
   dict_add(d, "password", session->peer->password);
   dict_add(d, "role", session->peer->parent ? "child" : "parent");
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

static void send_snapshot(struct link_session *session) {
   size_t capacity = rr_link_tree_count(links.tree) * 75 + 1;
   char *nodes = calloc(1, capacity);

   if (!nodes) {
      fail(session, "Out of memory");

      return;
   }
   rr_link_node_t node;

   for (size_t i = 0 ; rr_link_tree_node(links.tree, i, &node) ; i++) {
      size_t n = strlen(nodes);
      int added = snprintf(nodes + n, capacity - n, "%s%s,%s", i ? " " : "", node.server, node.parent ? node.parent : "-");

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

static void announce_node(struct link_session *exclude, const char *id, const char *parent) {
   dict *d = message("link.introduce");

   if (!d) {
      return;
   }
   dict_add(d, "node", id);
   dict_add(d, "parent", parent);
   broadcast(exclude, d);
   dict_free(d);
}

static void establish(struct link_session *session) {
   if (!session->snapshot || !session->ready || session->established) {
      return;
   }
   session->established = true;

   if (session->peer->parent) {
      strcpy(links.root, session->root);
   }
   Log(LOG_INFO, "link", "Linked site %s (%s), root %s, servers %zu", session->peer->site, session->node, links.root, rr_link_tree_count(links.tree));
   /* Publish this component in parent-before-child order. */
   bool sent[256] = {
      false
   };

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
            announce_node(session, node.server, node.parent);
            sent[i] = true;
         }
      }
   }

   event_emit("server.link.ready", NULL, session->node);
}

static void handle_hello(struct link_session *session, dict *d) {
   const char *site = dict_get(d, "site", ""), *node = dict_get(d, "node", ""), *root = dict_get(d, "root", ""), *role = dict_get(d, "role", "");

   if (session->hello || !rr_object_uuid_valid(node) || !strcmp(node, links.node) || strlen(site) >= 128 || !strcmp(site, links.site)) {
      fail(session, "Invalid peer identity");

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

   if (!peer || strcmp(peer->site, site) || (peer->node[0] && strcmp(peer->node, node)) ||
      !equal_secret(peer->username, dict_get(d, "username", "")) ||
      !equal_secret(peer->password, dict_get(d, "password", "")) || strcmp(role, peer->parent ? "parent" : "child")) {
      fail(session, "Peer authentication failed");

      return;
   }

   if ((peer->parent && !rr_object_uuid_valid(root)) || (!peer->parent && (!links.root[0] || *root))) {
      fail(session, "Parent is not rooted yet");

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
         fail(session, "Another topology admission is pending; retry");

         return;
      }
   }

   peer->session = session;
   session->peer = peer;
   strcpy(session->node, node);
   snprintf(session->root, sizeof(session->root), "%s", root);
   session->hello = true;

   if (!session->outbound) {
      send_hello(session);
   }
   send_snapshot(session);
}

static void handle_snapshot(struct link_session *session, dict *d) {
   if (!session->hello || session->snapshot || session->generation != links.generation) {
      fail(session, "Stale or unexpected snapshot; retry admission");

      return;
   }
   char *copy = strdup(dict_get(d, "nodes", "")), *save = NULL;
   rr_link_node_t nodes[256];
   size_t count = 0;
   bool includes_root = false;

   if (!copy) {
      fail(session, "Out of memory");

      return;
   }

   for (char *p = strtok_r(copy, " ", &save) ; p ; p = strtok_r(NULL, " ", &save)) {
      char *comma = strchr(p, ',');

      if (!comma || count == 256) {
         count = 0;
         break;
      }
      *comma++ = 0;
      nodes[count++] = (rr_link_node_t) {
         p, !strcmp(comma, "-") ? NULL : comma
      };

      if (!strcmp(p, session->root)) {
         includes_root = true;
      }
   }

   if (!count || (session->peer->parent && !includes_root) || rr_link_tree_attach(links.tree, session->node, nodes, count) != RR_LINK_OK) {
      free(copy);
      fail(session, "Topology overlap, cycle, missing root or capacity limit");

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
      fail(session, "Invalid link wire message");

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

      if (rr_link_tree_add(links.tree, session->node, node, parent) != RR_LINK_OK) {
         fail(session, "Conflicting introduction");
      } else {
         links.generation++;
         broadcast(session, d);
         Log(LOG_INFO, "link", "Introduced server %s via %s, servers %zu", node, session->peer->site, rr_link_tree_count(links.tree));
      }
   } else if (session->established && !strcmp(op, "link.withdraw")) {
      const char *node = dict_get(d, "node", "");

      if (!strcmp(node, session->node) || !strcmp(node, links.root)) {
         fail(session, "Upstream withdrew itself or the tree root");
      } else if (!rr_link_tree_remove(links.tree, session->node, node)) {
         fail(session, "Invalid subtree withdrawal");
      } else {
         links.generation++;
         broadcast(session, d);
         event_emit("server.link.lost", NULL, node);
      }
   } else if (session->established && !strcmp(op, "link.application")) {
      const char *origin = dict_get(d, "node", ""), *route = rrserver_links_route(origin);

      if (!route || strcmp(route, session->node)) {
         fail(session, "Application origin outside peer branch");
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
      session->peer->retry_at = mono_ms() + cfg_get_int("links.retry-seconds", 5) * 1000LL;

      if (session->peer->parent && session->admitted) {
         links.root[0] = 0;

         /* Children must not advertise stale reachability to the root. */
         for (struct link_session *s = links.sessions ; s ; s = s->next) {
            if (s != session) {
               s->connection->is_closing = 1;
            }
         }
      }
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

   if (event == MG_EV_CONNECT && session->outbound && !strncmp(session->peer->url, "wss://", 6)) {
      struct mg_tls_opts options = {
         .ca = links.ca, .name = mg_url_host(session->peer->url)
      };
      mg_tls_init(c, &options);
   } else if (event == MG_EV_HTTP_MSG) {
      struct mg_http_message *hm = data;
      struct mg_str *protocol = mg_http_get_header(hm, "Sec-WebSocket-Protocol");

      if (mg_match(hm->uri, mg_str("/link"), NULL) && protocol && !mg_strcmp(*protocol, mg_str(RR_LINK_SUBPROTOCOL))) {
         mg_ws_upgrade(c, hm, "Sec-WebSocket-Protocol: " RR_LINK_SUBPROTOCOL "\r\n");
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
      fail(session, "Transport error");
   } else if (event == MG_EV_CLOSE) {
      c->fn_data = NULL;
      close_session(session);
   }
}

static bool load_peer(const char *section) {
   const char *enabled = peer_value(section, "enabled");

   if (!enabled || !strcasecmp(enabled, "false")) {
      return true;
   }

   if (strcasecmp(enabled, "true")) {
      return false;
   }

   if (links.peer_count == MAX_PEERS) {
      return false;
   }
   struct link_peer *peer = &links.peers[links.peer_count++];
   snprintf(peer->section, sizeof(peer->section), "%s", section);
   const char *site = peer_value(section, "site.name"), *node = peer_value(section, "node.uuid"), *role = peer_value(section, "role");

   if (!site || !*site || strlen(site) >= sizeof(peer->site) || !strcmp(site, links.site) || (role && strcmp(role, "parent") && strcmp(role, "child"))) {
      return false;
   }
   strcpy(peer->site, site);
   peer->parent = !role || !strcmp(role, "parent");

   if (peer->parent && links.has_parent) {
      return false;
   }
   links.has_parent |= peer->parent;

   if (node && *node) {
      if (!rr_object_uuid_valid(node)) {
         return false;
      }
      snprintf(peer->node, sizeof(peer->node), "%s", node);
   }

   for (size_t i = 0 ; i + 1 < links.peer_count ; i++) {
      if (!strcmp(links.peers[i].site, site)) {
         return false;
      }
   }

   const char *url = peer_value(section, "server.url"), *username = peer_value(section, "username"),
      *password = peer_value(section, "password"), *autoconnect = peer_value(section, "autoconnect");

   if (!username || !*username || strlen(username) > 256 || !password || !*password || strlen(password) > 256) {
      return false;
   }

   if (url && *url && strncmp(url, "wss://", 6) && !(links.insecure && !strncmp(url, "ws://", 5))) {
      return false;
   }
   peer->url = strdup(url ? url : "");
   peer->username = strdup(username);
   peer->password = strdup(password);

   if (autoconnect && strcasecmp(autoconnect, "true") && strcasecmp(autoconnect, "false")) {
      return false;
   }
   peer->autoconnect = autoconnect && !strcasecmp(autoconnect, "true");

   return peer->url && peer->username && peer->password;
}

/* Shared client HTTP/TLS listeners hand /link sockets to the peer protocol. TLS is already established here; peers never become authenticated clients. */
static void shared_link_request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;

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

   if (!links.has_parent) {
      strcpy(links.root, links.node);
   }
   const char *listen = cfg_get("links.listen");

   if (!links.peer_count && (!listen || !*listen)) {
      return true;
   }
   int retry = cfg_get_int("links.retry-seconds", 5), timeout = cfg_get_int("links.handshake-seconds", 10);

   if (retry < 1 || retry > 3600 || timeout < 1 || timeout > 120) {
      return false;
   }
   links.tree = rr_link_tree_new(node, cfg_get_int("links.max-servers", 128));

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

   return true;
}

void rrserver_links_poll(void) {
   if (!links.tree || links.stopping) {
      return;
   }
   long long at = mono_ms();

   for (struct link_session *s = links.sessions ; s ; s = s->next) {
      if (!s->established && at - s->started > cfg_get_int("links.handshake-seconds", 10) * 1000LL) {
         fail(s, "Handshake timed out");
      }

      if (s->established && at - s->last_seen > 30000) {
         fail(s, "Link heartbeat timed out");
      }

      if (s->established && at - s->last_ping > 10000) {
         mg_ws_send(s->connection, "", 0, WEBSOCKET_OP_PING);
         s->last_ping = at;
      }
   }

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      struct link_peer *peer = &links.peers[i];

      if (!peer->autoconnect || !peer->url[0] || peer->session || at < peer->retry_at) {
         continue;
      }

      if (!peer->parent && !links.root[0]) {
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
   event_off_binary(RR_SERVER_HTTP_EVENT, shared_link_request, NULL);
   /* Call after mg_mgr_free: CLOSE callbacks own session cleanup. */
   rr_link_tree_free(links.tree);
   links.tree = NULL;

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      free(links.peers[i].url);
      free(links.peers[i].username);
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
   (void)node;

   return NULL;
}
bool rrserver_links_send(const char *node, dict *d) {
   (void)node;
   (void)d;

   return false;
}
void rrserver_links_broadcast(dict *d) {
   (void)d;
}
bool rrserver_links_init(struct mg_mgr *manager) {
   (void)manager;

   return true;
}
void rrserver_links_poll(void) {
}
void rrserver_links_fini(void) {
}
#endif
