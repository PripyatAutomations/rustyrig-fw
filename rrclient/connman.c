//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librrprotocol/wire.h>
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
#include <rrclient/frontend.h>
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
   char nick_base[NICKLEN + 1];
   unsigned nick_suffix;
   time_t nick_deadline;
   bool nick_recovering;
   void *rooms, *users, *vfos, *objects, *media, *wire_rooms;
   struct client_server *next;
} client_server_t;

static client_server_t *servers, *selected, *context;
static unsigned dispatch_depth;
static bool reconnect_attempting;

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

void rrclient_connection_dispatch(rrconn_t *connection, void (*callback) (void *), void *data) {
   if (!callback) {
      return;
   }
   client_server_t *target = NULL;

   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->connection == connection) {
         target = s;
         break;
      }
   }

   if (!target) {
      callback(data);

      return;
   }
   client_server_t *previous = context;
   activate(target);
   dispatch_depth++;
   callback(data);
   dispatch_depth--;
   activate(dispatch_depth ? previous : selected);
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
   bool server_status;
   client_server_t *server;
   struct client_window *next;
} client_window_t;
static client_window_t *windows;
static unsigned window_id;

static bool window_room_equal(const client_window_t *window, const char *room) {
   bool is_channel = room && (room[0] == '#' || room[0] == '&');
   bool is_irc = !window->server->connection || !window->server->connection->is_ws;

   if (is_irc && is_channel && (window->room[0] == '#' || window->room[0] == '&')) {
      return !strcasecmp(window->room, room);
   }

   return !strcmp(window->room, room);
}

const char *rrclient_window_name(const char *room) {
   if (!room || !*room || !strcasecmp(room, "status") || !context) {
      return room;
   }

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (w->server == context && window_room_equal(w, room)) {
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

/* IRC server notices can have their own tab, separate from shared status. */
void rrclient_server_status_window(const char *room) {
   const char *key = rrclient_window_name(room);

   if (!key || !room || !*room || !strcasecmp(room, "status")) {
      return;
   }

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (w->server == context && !strcmp(w->key, key)) {
         w->server_status = true;

         return;
      }
   }
}

const char *rrclient_window_room(const char *window) {
   for (client_window_t *w = windows ; w ; w = w->next) {
      if (window && !strcmp(w->key, window)) {
         return w->room;
      }
   }

   return window;
}

rrconn_t *rrclient_connection_for_window(const char *window) {
   if (!window || !*window) {
      return NULL;
   }

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (!strcmp(w->key, window)) {
         return w->server->connection;
      }
   }

   return NULL;
}

bool rrclient_window_is_status(const char *window) {
   for (client_window_t *w = windows ; w ; w = w->next) {
      if (window && !strcmp(w->key, window)) {
         return w->server_status;
      }
   }

   return false;
}

const char *rrclient_window_server(const char *window) {
   for (client_window_t *w = windows ; w ; w = w->next) {
      if (window && !strcmp(w->key, window)) {
         return w->server->name;
      }
   }

   return NULL;
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

static void nick_prompt_close(client_server_t *s) {
   s->nick_deadline = 0;

   if (frontend_present() && frontend_ops()->irc_nick_prompt) {
      frontend_ops()->irc_nick_prompt(s->name, NULL);
   }
}

static void nick_try_suffix(client_server_t *s) {
   static const char suffixes[] = "_{}[]-^";
   char nickname[NICKLEN + 1];
   unsigned index = s->nick_suffix++;

   if (index >= 56) {
      ui_print(NULL, "IRC nickname alternatives exhausted; disconnecting %s", s->name);
      s->nick_recovering = false;
      disconnect_server(s->name);

      return;
   }

   if (index < 7) {
      snprintf(nickname, sizeof(nickname), "%.*s%c", NICKLEN - 1, s->nick_base, suffixes[index]);
   } else {
      index -= 7;
      snprintf(nickname, sizeof(nickname), "%.*s%c%c", NICKLEN - 2, s->nick_base, suffixes[index / 7], suffixes[index % 7]);
   }

   if (!irc_send(s->connection, "NICK %s", nickname)) {
      disconnect_server(s->name);
   }
}

bool rrclient_irc_nick_submit(const char *profile, const char *nick) {
   client_server_t *s = find_server(profile);

   if (!s || !s->connection || s->connection->is_ws || !nick || !*nick ||
      strlen(nick) > NICKLEN || strpbrk(nick, " \t\r\n,*?!@:") ||
      !((nick[0] >= 'A' && nick[0] <= 'Z') || (nick[0] >= 'a' && nick[0] <= 'z') || strchr("_{}[]^\\`", nick[0]))) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)nick ; *p ; p++) {
      if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr("_{}[]-^\\`|", *p))) {
         return false;
      }
   }

   if (!irc_send(s->connection, "NICK %s", nick)) {
      return false;
   }
   s->nick_deadline = 0;
   snprintf(s->nick_base, sizeof(s->nick_base), "%s", nick);
   s->nick_suffix = 0;

   return true;
}

static void nick_accepted(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)user;

   if (!connection || connection->is_ws || !data) {
      return;
   }
   dict *message = json2dict(data);

   if (!message) {
      return;
   }
   const char *prefix = dict_get(message, "msg.prefix", "");
   size_t length = strcspn(prefix, "!");

   if (length == strlen(connection->nick) && !strncasecmp(prefix, connection->nick, length)) {
      for (client_server_t *s = servers ; s ; s = s->next) {
         if (s->connection == connection) {
            nick_prompt_close(s);
            s->nick_recovering = false;
         }
      }
   }
   dict_free(message);
}

static void nick_in_use(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;
   client_server_t *s = NULL;

   for (client_server_t *entry = servers ; entry ; entry = entry->next) {
      if (entry->connection == cptr) {
         s = entry;
      }
   }

   if (!s || !cptr || cptr->is_ws || !data) {
      return;
   }
   dict *message = json2dict(data);

   if (!message) {
      return;
   }
   const char *nickname = dict_get(message, "msg.arg2", "");
   ui_print(NULL, "IRC nickname %s is already in use on %s; use /nick <nickname> to choose another", nickname, s->name);

   if (!s->nick_recovering) {
      s->nick_recovering = true;
      s->nick_suffix = 0;
      snprintf(s->nick_base, sizeof(s->nick_base), "%s", nickname);

      if (frontend_present() && frontend_ops()->irc_nick_prompt) {
         s->nick_deadline = now + 45;
         frontend_ops()->irc_nick_prompt(s->name, nickname);
      } else {
         nick_try_suffix(s);
      }
   } else if (!s->nick_deadline) {
      nick_try_suffix(s);
   }
   dict_free(message);
}

static void connection_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)data;
   (void)user;
   client_server_t *s = context;

   if (!s || (cptr && cptr != s->connection)) {
      return;
   }

   if (!strcmp(event, "authorized") || !strcmp(event, "irc.connected") || !strcmp(event, "disconnected")) {
      nick_prompt_close(s);
      s->nick_recovering = false;
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
      s->reconnect_tries++;
      /* PARITY: rustyrig-www/js/webui.js reconnect interval precedence. */
      const char *override = get_server_property(s->name, "server.reconnect-interval");
      char *end = NULL;
      long interval = override && *override ? strtol(override, &end, 10) : cfg_get_int("server.reconnect-interval", 60);

      if (interval < 1 || interval > 3600 || (end && *end)) {
         interval = 60;
      }
      s->reconnect_pending = true;
      s->reconnect_at = now + interval;
      ws_connected = -1;
      ui_print(NULL, "Reconnecting in %ld seconds (attempt %u/%u)", interval, s->reconnect_tries, RRC_MAX_RECONNECTS);
   }
}

void connman_register_events(void) {
   event_on("irc.nick-in-use", nick_in_use, NULL);
   event_on("irc.nick", nick_accepted, NULL);
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
   bool proxy_pending, proxy_error_shown;
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
   if (ev == MG_EV_WS_MSG && s != selected && data && ((((struct mg_ws_message *)data)->flags & 15) == WEBSOCKET_OP_BINARY)) {
      return;
   }
   client_server_t *previous = context;
   activate(s);
   dispatch_depth++;

   if (ev == MG_EV_OPEN) {
      cptr->conn = c;
   }

   char proxy_error[512];

   if (t->proxy_pending && (ev == MG_EV_ERROR || ev == MG_EV_CLOSE)) {
      const char *reason = ev == MG_EV_ERROR && data ? data : "connection closed before the SOCKS5 tunnel was established";
      size_t length = strlen(t->socks.pass), written = 0;
      while (*reason && written + 1 < sizeof(proxy_error)) {
         if (length && !strncmp(reason, t->socks.pass, length)) {
            if (written + 3 >= sizeof(proxy_error)) {
               break;
            }
            memcpy(proxy_error + written, "***", 3);
            written += 3;
            reason += length;
         } else {
            proxy_error[written++] = *reason++;
         }
      }
      proxy_error[written] = '\0';

      if (!t->proxy_error_shown) {
         ui_print(NULL, "SOCKS5 proxy %s for %s:%d failed: %s", t->socks.url, t->server.host, t->server.port, proxy_error);
         t->proxy_error_shown = true;
      }

      if (ev == MG_EV_ERROR) {
         data = proxy_error;
      }
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
      if (s->nick_deadline && now >= s->nick_deadline) {
         nick_prompt_close(s);
         nick_try_suffix(s);
      }

      if (s->reconnect_pending && now >= s->reconnect_at && !dying && (!s->connection || !s->connection->conn)) {
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
   nick_prompt_close(s);
   s->nick_recovering = false;
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

   client_server_t *previous = context;
   activate(s);
   dispatch_depth++;
   client_window_t **link = &windows;
   while (*link) {
      client_window_t *w = *link;

      if (w->server != s || !w->server_status) {
         link = &w->next;
         continue;
      }
      event_emit("client.server.status.close", s->connection, w->room);
      *link = w->next;
      free(w->room);
      free(w);
   }
   dispatch_depth--;
   activate(dispatch_depth ? previous : selected);

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
          left.credentials == right.credentials && !strcmp(left.user, right.user) && !strcmp(left.pass, right.pass) &&
          !strcasecmp(left.host, right.host) && (left.irc || !strcmp(left.path, right.path));
}

bool rrclient_connect_url(const char *profile, const char *url) {
   return rrclient_connect_url_proxy(profile, url, NULL);
}

bool rrclient_connect_url_proxy(const char *profile, const char *url, const char *proxy_override) {
   rr_server_url_t endpoint;

   if (profile && (!url || !*url)) {
      ui_print(NULL, "No server.url for profile '%s' in %s; use /server to list loaded profiles", profile, config_file ? config_file :
         "the loaded configuration");

      return true;
   }

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

   if (endpoint.credentials) {
      nick = endpoint.user;
      pass = endpoint.pass;
   }
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

   if (endpoint.irc) {
      const char *sasl_user = get_server_property(s->name, "server.sasl.user");
      const char *sasl_pass = get_server_property(s->name, "server.sasl.pass");
      const char *mechanism = get_server_property(s->name, "server.sasl.mechanism");

      if (!mechanism || !*mechanism) {
         mechanism = "PLAIN";
      }

      if ((sasl_user && strlen(sasl_user) >= sizeof(t->server.sasl_user)) ||
         (sasl_pass && strlen(sasl_pass) >= sizeof(t->server.sasl_pass)) || (strcasecmp(mechanism, "PLAIN") && strcasecmp(mechanism, "EXTERNAL"))) {
         ui_print(NULL, "Invalid IRC SASL settings for %s", s->name);
         free(t);

         return true;
      }
      snprintf(t->server.sasl_user, sizeof(t->server.sasl_user), "%s", sasl_user ? sasl_user : "");
      snprintf(t->server.sasl_pass, sizeof(t->server.sasl_pass), "%s", sasl_pass ? sasl_pass : "");
      snprintf(t->server.sasl_mechanism, sizeof(t->server.sasl_mechanism), "%s", mechanism);
   }
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
   ui_print(NULL, "%s Connecting to %s", get_chat_ts(now), endpoint.credentials ? connect_url : url);
   const char *socket_url = t->proxy_pending ? t->socks.url : connect_url;
   struct mg_connection *c = endpoint.irc ? mg_connect(&mgr, socket_url, rrclient_transport_handler, t) :
      mg_ws_connect(&mgr, socket_url, rrclient_transport_handler, t, "Sec-WebSocket-Protocol: " RR_WS_SUBPROTOCOL "\r\n");

   if (c && t->proxy_pending && !endpoint.irc) {
      /* Keep Mongoose's WebSocket parser without running it on SOCKS replies. Its initial request describes the proxy; replace it with the destination. */
      t->websocket_handler = c->pfn;
      c->pfn = NULL;
      mg_iobuf_del(&c->send, 0, c->send.len);
      char nonce[16], key[30];
      mg_random(nonce, sizeof(nonce));
      mg_base64_encode((unsigned char *)nonce, sizeof(nonce), key, sizeof(key));
      mg_printf(c, "GET %s HTTP/1.1\r\nHost: %s%s%s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
         "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: " RR_WS_SUBPROTOCOL "\r\nSec-WebSocket-Key: %s\r\n\r\n", endpoint.path, endpoint.ipv6 ? "[" : "",
         endpoint.host, endpoint.ipv6 ? "]" : "", endpoint.port, key);
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
      nick_prompt_close(servers);

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
