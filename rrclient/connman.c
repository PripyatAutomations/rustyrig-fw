// Native connection manager. Wire handlers remain in librrprotocol.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/http.h>
#include <librrprotocol/irc.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <librustyaxe/socks.h>

extern const char *login_user;
extern bool dying;
extern char session_token[HTTP_TOKEN_LEN + 1];
extern void rrclient_update_connection_ui(int connected);
extern void tui_refresh_sb_online(void);

/* Component-owned views are exchanged only on the main event-loop thread. Weak hooks let headless transport probes omit unrelated client components. */
extern void rrclient_rooms_context_swap(void **) __attribute__((weak));
extern void rrclient_userlist_context_swap(void **) __attribute__((weak));
extern void rrclient_vfo_context_swap(void **) __attribute__((weak));
extern void rrclient_objects_context_swap(void **) __attribute__((weak));
extern void rrclient_media_context_swap(void **) __attribute__((weak));
extern void ws_rooms_context_swap(void **);
extern void rrclient_rooms_context_free(void *) __attribute__((weak));
extern void rrclient_userlist_context_free(void *) __attribute__((weak));
extern void rrclient_vfo_context_free(void *) __attribute__((weak));
extern void rrclient_objects_context_free(void *) __attribute__((weak));


typedef struct client_server {
   char *name, *url;
#ifdef USE_MONGOOSE
   rr_socks_t *proxy;
   bool proxy_override;
#endif
   rrconn_t *connection;
   const char *user;
   int connected;
   bool reconnect_enabled, reconnect_pending;
   unsigned reconnect_tries;
   time_t reconnect_at;
   void *rooms, *users, *vfos, *objects, *media, *wire_rooms;
   struct client_server *next;
} client_server_t;

static client_server_t *servers, *selected, *context;
static unsigned dispatch_depth;
static bool reconnect_attempting;
static const unsigned reconnect_delays[] = {
   1, 2, 5, 10, 30, 60
};
#define RRC_MAX_RECONNECTS 10

static client_server_t *find_server(const char *name) {
   for (client_server_t *s = servers ; s ; s = s->next) {
      if (name && !strcmp(s->name, name)) {
         return s;
      }
   }

   return NULL;
}

static void exchange_views(client_server_t *s) {
   if (rrclient_rooms_context_swap) {
      rrclient_rooms_context_swap(&s->rooms);
   }

   if (rrclient_userlist_context_swap) {
      rrclient_userlist_context_swap(&s->users);
   }

   if (rrclient_vfo_context_swap) {
      rrclient_vfo_context_swap(&s->vfos);
   }

   if (rrclient_objects_context_swap) {
      rrclient_objects_context_swap(&s->objects);
   }

   if (rrclient_media_context_swap) {
      rrclient_media_context_swap(&s->media);
   }
   ws_rooms_context_swap(&s->wire_rooms);
}

static void activate(client_server_t *s) {
   if (context == s) {
      return;
   }

   if (context) {
      context->user = login_user;
      context->connected = ws_connected;
      exchange_views(context);
   }
   context = s;
   free((void *)server_name);
   server_name = s ? strdup(s->name) : NULL;
   login_user = s ? s->user : NULL;
   ws_conn = s ? s->connection : NULL;
   ws_connected = s ? s->connected : 0;
   memset(session_token, 0, sizeof(session_token));

   if (s) {
      if (s->connection) {
         memcpy(session_token, s->connection->token, sizeof(session_token));
      }
      exchange_views(s);
   }
}

bool rrclient_context_is_selected(void) {
   return !context || context == selected;
}

const char *rrclient_selected_server(void) {
   return selected ? selected->name : NULL;
}

const char *rrclient_connection_name(const rrconn_t *connection) {
   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->connection == connection) {
         return s->name;
      }
   }

   return NULL;
}

rrconn_t *rrclient_connection_find(const char *name) {
   client_server_t *s = find_server(name);

   return s ? s->connection : NULL;
}

int rrclient_connection_state(const char *name) {
   client_server_t *s = find_server(name);

   return s ? (s == context ? ws_connected : s->connected) : 0;
}

const char *rrclient_connection_iter(unsigned index) {
   for (client_server_t *s = servers ; s ; s = s->next) {
      if (!index--) {
         return s->name;
      }
   }

   return NULL;
}

bool rrclient_connection_select(const char *name) {
   client_server_t *s = find_server(name);

   if (!s) {
      return false;
   }

   if (dispatch_depth) {
      return s == selected;
   }

   if (selected == s && context == s) {
      return true;
   }
   selected = s;

   if (!dispatch_depth) {
      activate(s);
      rrclient_update_connection_ui(ws_connected);
      tui_refresh_sb_online();
   }
   event_emit("client.server.selected", s->connection, s->name);

   return true;
}

bool rrclient_connection_cycle_status(bool status_active) {
   return status_active && rrclient_connection_cycle();
}

bool rrclient_connection_cycle(void) {
#ifndef USE_MONGOOSE

   return false;
#else

   if (!servers) {
      return false;
   }
   client_server_t *next = selected && selected->next ? selected->next : servers;
   client_server_t *first = next;
   do {
      if (next->connection && next->connection->conn && !next->connection->conn->is_closing) {
         return rrclient_connection_select(next->name);
      }
      next = next->next ? next->next : servers;
   } while (next != first);
   return false;
#endif
}

/* Stable, bounded window keys keep equal room names on different servers separate without changing their wire names or storing stale socket pointers. */
typedef struct client_window {
   char key[64];
   char *room;
   client_server_t *server;
   struct client_window *next;
} client_window_t;
static client_window_t *windows;
static unsigned window_id;

const char *rrclient_window_name(const char *room) {
   if (!room || !*room || !strcasecmp(room, "status") || !context) {
      return room;
   }

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (w->server == context && !strcmp(w->room, room)) {
         return w->key;
      }
   }

   client_window_t *w = calloc(1, sizeof(*w));

   if (!w || !(w->room = strdup(room))) {
      free(w);

      return NULL;
   }
   w->server = context;
   snprintf(w->key, sizeof(w->key), "%.32s|%.18s|%u", room, context->name, ++window_id);
   w->next = windows;
   windows = w;

   return w->key;
}

const char *rrclient_window_room(const char *window) {
   for (client_window_t *w = windows ; w ; w = w->next) {
      if (window && !strcmp(w->key, window)) {
         return w->room;
      }
   }

   return window;
}

void rrclient_connection_select_window(const char *window) {
   if (dispatch_depth) {
      return;
   }

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (window && !strcmp(w->key, window)) {
         rrclient_connection_select(w->server->name);

         return;
      }
   }
}

static void cancel_reconnect(client_server_t *s) {
   if (!s) {
      return;
   }
   s->reconnect_enabled = s->reconnect_pending = false;
   s->reconnect_tries = 0;
}

static void connection_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)data;
   (void)user;
   client_server_t *s = context;

   if (!s || (cptr && cptr != s->connection)) {
      return;
   }

   if (!strcmp(event, "auth.error")) {
      cancel_reconnect(s);
   } else if (!strcmp(event, "authorized") || !strcmp(event, "irc.connected")) {
      s->reconnect_pending = false;
      s->reconnect_tries = 0;
      ws_connected = 1;
   } else if (s->reconnect_enabled && !s->reconnect_pending && !dying) {
      if (s->reconnect_tries >= RRC_MAX_RECONNECTS) {
         cancel_reconnect(s);
         ui_print(NULL, "Giving up after %u reconnect attempts", RRC_MAX_RECONNECTS);

         return;
      }
      unsigned i = s->reconnect_tries++;

      if (i >= sizeof(reconnect_delays) / sizeof(*reconnect_delays)) {
         i = sizeof(reconnect_delays) / sizeof(*reconnect_delays) - 1;
      }
      s->reconnect_pending = true;
      s->reconnect_at = now + reconnect_delays[i];
      ws_connected = -1;
      ui_print(NULL, "Reconnecting in %u seconds (attempt %u/%u)", reconnect_delays[i], s->reconnect_tries, RRC_MAX_RECONNECTS);
   }
}

void connman_register_events(void) {
   event_on("authorized", connection_event, NULL);
   event_on("irc.connected", connection_event, NULL);
   event_on("disconnected", connection_event, NULL);
   event_on("http.error", connection_event, NULL);
   event_on("auth.error", connection_event, NULL);
}

#ifdef USE_MONGOOSE
extern struct mg_mgr mgr;
extern void http_handler(struct mg_connection *, int, void *);
typedef struct {
   rrconn_t connection;
   server_cfg_t server;
   client_server_t *owner;
   rr_socks_t socks;
   bool proxy_pending;
   mg_event_handler_t websocket_handler;
   struct mg_iobuf websocket_request;
} rrclient_transport_t;

static void rrclient_transport_handler(struct mg_connection *c, int ev, void *data) {
   rrclient_transport_t *t = c->fn_data;

   if (!t) {
      return;
   }
   client_server_t *s = t->owner;
   rrconn_t *cptr = &t->connection;

   if (s->connection != cptr) {
      if (ev == MG_EV_CLOSE) {
         irc_capabilities_clear(cptr);
         mg_iobuf_free(&t->websocket_request);
         free(t);
         c->fn_data = NULL;
      }

      return;
   }

   /* Only the selected server feeds shared audio/serial output devices. Text updates still populate every server's independent protocol/client state. */
   if (ev == MG_EV_WS_MSG && s != selected && data &&
      ((((struct mg_ws_message *)data)->flags & 15) == WEBSOCKET_OP_BINARY)) {
      return;
   }
   client_server_t *previous = context;
   activate(s);
   dispatch_depth++;

   if (ev == MG_EV_OPEN) {
      cptr->conn = c;
   }

   if (t->proxy_pending && (ev == MG_EV_ERROR || ev == MG_EV_CLOSE)) {
      rr_socks_event(&t->socks, c, ev);
   }

   if (t->proxy_pending && ev != MG_EV_ERROR && ev != MG_EV_CLOSE && ev != MG_EV_OPEN) {
      int result = rr_socks_event(&t->socks, c, ev);

      if (result != 1) {
         dispatch_depth--;
         activate(dispatch_depth ? previous : selected);

         return;
      }
      t->proxy_pending = false;
      c->is_tls = t->server.tls;
      c->pfn = t->websocket_handler;

      if (cptr->is_ws) {
         /* Release the destination's upgrade request only inside the tunnel. */
         mg_send(c, t->websocket_request.buf, t->websocket_request.len);
         mg_iobuf_free(&t->websocket_request);
         http_handler(c, MG_EV_CONNECT, NULL);
      } else {
         irc_mongoose_handler(c, MG_EV_CONNECT, NULL);
      }

      /* Mongoose normally kicks TLS from its TCP-connect path; this CONNECT occurs later, after the SOCKS reply, so start ClientHello explicitly. */
      if (c->is_tls_hs && !c->is_closing) {
         mg_tls_handshake(c);
      }

      /* IRC greetings may share a TCP read with the SOCKS CONNECT reply. */
      if (!c->is_tls && c->recv.len) {
         if (c->pfn) {
            c->pfn(c, MG_EV_READ, data);
         }

         if (cptr->is_ws) {
            http_handler(c, MG_EV_READ, data);
         } else {
            irc_mongoose_handler(c, MG_EV_READ, data);
         }
      }
      dispatch_depth--;
      s->connected = ws_connected;
      activate(dispatch_depth ? previous : selected);

      return;
   }

   if (cptr->is_ws) {
      http_handler(c, ev, data);
   } else {
      irc_mongoose_handler(c, ev, data);
   }
   dispatch_depth--;

   if (ev == MG_EV_CLOSE) {
      mg_iobuf_free(&t->websocket_request);
      cptr->conn = NULL;
      /* Keep the offline profile selectable, but never retain a dead socket. */
   }
   s->connected = ws_connected;
   activate(dispatch_depth ? previous : selected);
}
#endif

void rrclient_poll_events_reconnect(void) {
#ifdef USE_MONGOOSE

   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->reconnect_pending && now >= s->reconnect_at && !dying &&
         (!s->connection || !s->connection->conn)) {
         s->reconnect_pending = false;

         if (s->reconnect_enabled) {
            reconnect_attempting = true;
            rrclient_connect_url(s->name, s->url);
            reconnect_attempting = false;
         }
      }
   }

#endif
}

bool disconnect_server(const char *name) {
   client_server_t *s = name && *name ? find_server(name) : selected;

   if (!s) {
      return true;
   }
   cancel_reconnect(s);
#ifdef USE_MONGOOSE

   if (s->connection && s->connection->conn) {
      s->connection->conn->is_closing = 1;
   }
#endif
   s->connected = 0;

   if (s == context) {
      ws_connected = 0;
   }

   return false;
}

bool connect_server(const char *name) {
   if (!name || !*name) {
      name = rrclient_selected_server();
   }

   if (!name) {
      return true;
   }

   if (strstr(name, "://")) {
      return rrclient_connect_url(NULL, name);
   }

   return rrclient_connect_url(name, get_server_property(name, "server.url"));
}

static bool same_url(const char *a, const char *b) {
   rr_server_url_t left, right;

   return rr_server_url_parse(a, &left) && rr_server_url_parse(b, &right) &&
          left.transport == right.transport && left.port == right.port &&
          !strcasecmp(left.host, right.host) && (left.irc || !strcmp(left.path, right.path));
}

bool rrclient_connect_url(const char *profile, const char *url) {
   return rrclient_connect_url_proxy(profile, url, NULL);
}

bool rrclient_connect_url_proxy(const char *profile, const char *url, const char *proxy_override) {
   rr_server_url_t endpoint;

   if (!rr_server_url_parse(url, &endpoint)) {
      ui_print(NULL, "\00304Invalid server.url: use ws://host/path, wss://host/path, irc://host or ircs://host (optional :port)\017");

      return true;
   }
#ifdef USE_MONGOOSE

   /* A configured profile name takes precedence over the URL hostname. */
   if (!profile || !*profile) {
      int rank = 0;
      const char *key;
      char *value;
      while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
         size_t n = strlen(key);

         if (!strncmp(key, "server:", 7) && n > 18 && !strcmp(key + n - 11, ".server.url") && value && same_url(value, url)) {
            static char matched[512];
            snprintf(matched, sizeof(matched), "%.*s", (int)(n - 18), key + 7);
            profile = matched;
            break;
         }
      }

      if (!profile || !*profile) {
         profile = endpoint.host;
      }
   }
   client_server_t *s = find_server(profile);

   const char *proxy = get_server_property(profile, "server.proxy");

   if (!proxy || !*proxy) {
      proxy = cfg_get("server.proxy");
   }
   const char *proxy_user = get_server_property(profile, "server.proxy.user");
   const char *proxy_pass = get_server_property(profile, "server.proxy.pass");

   if (!proxy_user) {
      proxy_user = cfg_get("server.proxy.user");
   }

   if (!proxy_pass) {
      proxy_pass = cfg_get("server.proxy.pass");
   }

   char pinned_url[1024];
   const rr_socks_t *pinned = s ? s->proxy : NULL;

   if (proxy_override) {
      if (!*proxy_override) {
         ui_print(NULL, "The -proxy option requires a nonempty SOCKS5 URL");

         return true;
      }
      proxy = proxy_override;
   } else if (pinned && (s->proxy_override || reconnect_attempting || !proxy || !*proxy)) {
      snprintf(pinned_url, sizeof(pinned_url), "socks5h://%s", pinned->url + 6);
      proxy = pinned_url;
      proxy_user = pinned->user;
      proxy_pass = pinned->pass;
   }
   bool proxy_pending = proxy && *proxy;
   rr_socks_t socks;

   if (proxy_pending && !rr_socks_init(&socks, proxy, proxy_user, proxy_pass, endpoint.host, endpoint.port)) {
      if (s && !s->proxy && s->connection && s->connection->conn) {
         s->connection->conn->is_closing = 1;
      }
      ui_print(NULL, "Invalid server.proxy: use socks5h://host[:port] or socks5://host[:port], with optional proxy user/pass keys");

      return true;
   }

   if (s && proxy_pending && (!s->proxy || (proxy_override &&
      (strcmp(s->proxy->url, socks.url) || strcmp(s->proxy->user, socks.user) || strcmp(s->proxy->pass, socks.pass)))) &&
      s->connection && s->connection->conn) {
      s->connection->conn->is_closing = 1;
   }

   if (s && s->connection && s->connection->conn && !s->connection->conn->is_closing) {
      if (!same_url(s->url, url)) {
         ui_print(NULL, "Server name %s is already connected; use a distinct configuration section for another endpoint", profile);

         return true;
      }

      if (proxy_override) {
         s->proxy_override = true;
      }
      rrclient_connection_select(s->name);

      return false;
   }
   char *saved_url = strdup(url);

   if (!saved_url) {
      return true;
   }

   if (!s) {
      s = calloc(1, sizeof(*s));

      if (!s) {
         free(saved_url);

         return true;
      }
      s->name = strdup(profile);

      if (!s->name) {
         free(s);
         free(saved_url);

         return true;
      }
      client_server_t **tail = &servers;
      while (*tail) {
         tail = &(*tail)->next;
      }
      *tail = s;
   }
   free(s->url);
   s->url = saved_url;
   url = s->url;
   rr_server_url_parse(url, &endpoint);
   rrclient_transport_t *t = calloc(1, sizeof(*t));

   if (!t) {
      return true;
   }
   t->owner = s;
   t->connection.fd = -1;
   t->connection.is_ws = !endpoint.irc;
   /* Every transport owns its profile/host, including secure WebSockets. */
   t->connection.server = &t->server;
   snprintf(t->server.host, sizeof(t->server.host), "%s", endpoint.host);
   snprintf(t->server.network, sizeof(t->server.network), "%s", s->name);
   t->server.port = endpoint.port;
   t->server.tls = endpoint.tls;
   const char *nick = get_server_property(s->name, "server.user");
   const char *pass = get_server_property(s->name, "server.pass");
   const char *autojoin = get_server_property(s->name, "autojoin");
   t->proxy_pending = proxy_pending;

   if (proxy_pending) {
      t->socks = socks;
   }

   if (t->proxy_pending) {
      if (!s->proxy) {
         s->proxy = malloc(sizeof(*s->proxy));
      }

      if (!s->proxy) {
         free(t);

         return true;
      }
      *s->proxy = t->socks;

      if (proxy_override) {
         s->proxy_override = true;
      }
   }

   if (endpoint.irc && (!nick || !*nick)) {
      nick = cfg_get("irc.nick");
   }

   if (!nick || !*nick) {
      nick = endpoint.irc ? "nonick" : "guest";
   }

   if (strlen(nick) >= sizeof(t->connection.nick) || (pass && strlen(pass) >= sizeof(t->server.pass)) ||
      (autojoin && strlen(autojoin) >= sizeof(t->server.autojoin))) {
      free(t);

      return true;
   }
   snprintf(t->connection.nick, sizeof(t->connection.nick), "%s", nick);
   snprintf(t->server.nick, sizeof(t->server.nick), "%s", nick);
   snprintf(t->server.pass, sizeof(t->server.pass), "%s", pass ? pass : "");
   snprintf(t->server.autojoin, sizeof(t->server.autojoin), "%s", autojoin ? autojoin : "");

   if (endpoint.irc && irc_init()) {
      free(t);

      return true;
   }
   rrconn_t *old = s->connection;
   s->connection = &t->connection;

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (w->server == s) {
         tui_window_t *window = tui_window_find(w->key);

         if (window) {
            window->cptr = s->connection;
         }
      }
   }

   if (old && !old->conn) {
      free(old);
   }
   bool reconnecting = reconnect_attempting;

   if (!reconnecting) {
      s->reconnect_tries = 0;
   }
   s->reconnect_enabled = true;
   s->reconnect_pending = false;
   s->connected = -1;

   if (!selected || !reconnecting) {
      selected = s;
   }
   activate(s);
   ws_conn = s->connection;
   ws_connected = -1;
   ui_print(NULL, "%s Connecting to %s", get_chat_ts(now), url);
   char connect_url[2048];
   const char *scheme = endpoint.irc ? (endpoint.tls ? "tls" : "tcp") : (endpoint.tls ? "wss" : "ws");
   int length = snprintf(connect_url, sizeof(connect_url), "%s://%s%s%s:%u%s", scheme, endpoint.ipv6 ? "[" : "", endpoint.host, endpoint.ipv6 ? "]" : "",
      endpoint.port, endpoint.irc ? "" : endpoint.path);

   if (length < 0 || (size_t)length >= sizeof(connect_url)) {
      free(t);
      s->connection = NULL;
      ws_conn = NULL;
      ws_connected = 0;
      activate(selected);

      return true;
   }
   dispatch_depth++;
   const char *socket_url = t->proxy_pending ? t->socks.url : connect_url;
   struct mg_connection *c = endpoint.irc ? mg_connect(&mgr, socket_url, rrclient_transport_handler, t) :
      mg_ws_connect(&mgr, socket_url, rrclient_transport_handler, t, NULL);

   if (c && t->proxy_pending && !endpoint.irc) {
      /* Keep Mongoose's WebSocket parser without running it on SOCKS replies. Its initial request describes the proxy; replace it with the destination. */
      t->websocket_handler = c->pfn;
      c->pfn = NULL;
      mg_iobuf_del(&c->send, 0, c->send.len);
      char nonce[16], key[30];
      mg_random(nonce, sizeof(nonce));
      mg_base64_encode((unsigned char *)nonce, sizeof(nonce), key, sizeof(key));
      mg_printf(c, "GET %s HTTP/1.1\r\nHost: %s%s%s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
         "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n", endpoint.path, endpoint.ipv6 ? "[" : "", endpoint.host, endpoint.ipv6 ? "]" : "",
         endpoint.port, key);
      t->websocket_request = c->send;
      memset(&c->send, 0, sizeof(c->send));
      c->send.align = t->websocket_request.align;
   }
   dispatch_depth--;

   if (!c) {
      ws_connected = 0;
      event_emit("http.error", s->connection, NULL);
      free(t);
      s->connection = NULL;
      ws_conn = NULL;
   } else {
      t->connection.conn = c;
   }
   s->connected = ws_connected;
   activate(selected);
   event_emit("client.server.selected", selected ? selected->connection : NULL, rrclient_selected_server());

   return !c;
#else
   (void)proxy_override;
   ui_print(NULL, "Server connections require the Mongoose transport backend");

   return true;
#endif
}

bool connect_or_disconnect(const char *name) {
   client_server_t *s = find_server(name);
#ifdef USE_MONGOOSE

   if (s && s->connection && s->connection->conn && !s->connection->conn->is_closing) {
      return disconnect_server(name);
   }
#else
   (void)s;
#endif

   return connect_server(name);
}

void connman_autoconnect(void) {
   char *list = (char *)cfg_get_exp("server.auto-connect");

   if (!list || !*list) {
      free(list);
      show_server_chooser();

      return;
   }
   char *save = NULL;

   for (char *name = strtok_r(list, ", \t", &save) ; name ; name = strtok_r(NULL, ", \t", &save)) {
      connect_server(name);
   }

   free(list);
}

void rrclient_quit_servers(const char *reason) {
#ifdef USE_MONGOOSE

   for (client_server_t *s = servers ; s ; s = s->next) {
      rrconn_t *c = s->connection;
      cancel_reconnect(s);

      if (c && !c->is_ws && c->sent_login && c->conn && !c->conn->is_closing && !c->conn->is_draining) {
         if (!irc_send(c, "QUIT :%s", reason ? reason : "Client exiting")) {
            irc_send(c, "QUIT :Client exiting");
         }
         c->conn->is_draining = 1;
      }
   }

#else
   (void)reason;
#endif
}

void rrclient_flush_quit(void) {
#ifdef USE_MONGOOSE
   uint64_t deadline = mg_millis() + 1000;
   while (mg_millis() < deadline) {
      bool pending = false;

      for (client_server_t *s = servers ; s ; s = s->next) {
         rrconn_t *c = s->connection;

         if (c && !c->is_ws && c->conn && c->conn->is_draining) {
            pending = true;
         }
      }

      if (!pending) {
         break;
      }
      mg_mgr_poll(&mgr, 10);
   }
#endif
}

/* Call after the transport manager has emitted final CLOSE notifications. */
void connman_shutdown(void) {
   selected = NULL;
   activate(NULL);
   while (windows) {
      client_window_t *next = windows->next;
      tui_window_t *window = tui_window_find(windows->key);

      if (window) {
         window->cptr = NULL;
      }
      free(windows->room);
      free(windows);
      windows = next;
   }
   while (servers) {
      client_server_t *next = servers->next;

      if (rrclient_rooms_context_free) {
         rrclient_rooms_context_free(servers->rooms);
      }

      if (rrclient_userlist_context_free) {
         rrclient_userlist_context_free(servers->users);
      }

      if (rrclient_vfo_context_free) {
         rrclient_vfo_context_free(servers->vfos);
      }

      if (rrclient_objects_context_free) {
         rrclient_objects_context_free(servers->objects);
      }
      free(servers->media);
      free(servers->wire_rooms);
      free((void *)servers->user);
      free(servers->connection);
      free(servers->name);
      free(servers->url);
#ifdef USE_MONGOOSE
      free(servers->proxy);
#endif
      free(servers);
      servers = next;
   }
}
