//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.mongoose.h>
#include <librustyaxe/transport.wss.h>
#include <librustyaxe/transport.tcp.h>
#include <librustyaxe/transport.h>
#include <librrprotocol/wire-version.h>
#include <librrprotocol/traffic.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/http.h>
#include <rrclient/irc.h>
#include <rrclient/connman.h>
#include <rrclient/connecting.h>
#include <rrclient/media.direct.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/tls.pin.h>
#include <rrclient/password.change.h>
#include <rrclient/database.h>
// Native connection manager. IRC wire handlers live in librustyirc.
#include <librrprotocol/wire.h>
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


#ifndef USE_MONGOOSE
typedef struct rrclient_native_connection rrclient_native_connection_t;
extern bool rrclient_native_text_receive(rrconn_t *cptr, const uint8_t *data, size_t len);
extern bool ws_binframe_process_client(rrconn_t *cptr, const char *data, size_t len);
#endif

typedef struct client_server {
   char *name, *url;
   char domain[254], server_uuid[37];
   char pending_tls_pin[65], temporary_tls_pin[65];
   bool pending_tls_pin_confirmation;
   char *pending_password_change; /* zeroed and released on reply or disconnect */
   bool password_prompt_open;
#ifndef USE_MONGOOSE
   char *native_proxy_override;
#endif
#ifdef USE_MONGOOSE
   rr_socks_t *proxy;
   bool proxy_override;
#endif
   rrconn_t *connection;
#ifndef USE_MONGOOSE
   rrclient_native_connection_t *native;
#endif
   const char *user;
   int connected;
   uint64_t traffic_rx, traffic_tx;
   bool reconnect_enabled, reconnect_pending;
   bool login_challenge_pending; /* handshake reached credential verification */
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
   /* Compatibility view, borrowed from the active profile. Avoid allocating and freeing this name on every nested connection dispatch. Profile names remain
    * alive until connman_shutdown(), which calls activate(NULL) before releasing them. */
   server_name = s ? s->name : NULL;
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

/* Transport byte totals scoped to one configured server, including IRC and WebSocket frames (not only text payloads). */
bool rrclient_connection_traffic(const char *name, uint64_t *rx, uint64_t *tx) {
   client_server_t *s = find_server(name);
   /* WebSocket bytes are recorded by librrprotocol on the connection, whereas the IRC socket path maintains the profile counters below. Do not mistake an
    * active WS transport for an idle one. */
   uint64_t incoming = s ? s->traffic_rx : 0, outgoing = s ? s->traffic_tx : 0;

   if (s && s->connection && s->connection->is_ws) {
      const struct rr_traffic *traffic = &s->connection->traffic;
      incoming = traffic->rx_text_bytes + traffic->rx_binary_bytes;
      outgoing = traffic->tx_text_bytes + traffic->tx_binary_bytes;
      /* Native RTP receive accounting also contributes to peer->traffic;
       * exclude it from the WS readout now that RTP has a separate meter. */
      uint64_t rtp_incoming = 0;
      rrclient_direct_traffic(s->connection, &rtp_incoming, NULL);
      incoming = incoming >= rtp_incoming ? incoming - rtp_incoming : 0;
   }

   if (rx) {
      *rx = incoming;
   }

   if (tx) {
      *tx = outgoing;
   }

   return s != NULL;
}
void rrclient_connection_count_traffic(const rrconn_t *connection, uint64_t rx, uint64_t tx) {
   if (!connection) {
      return;
   }

   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->connection == connection) {
         s->traffic_rx += rx;
         s->traffic_tx += tx;

         return;
      }
   }
}

/* Validate protocol UUIDs without depending on a newer librrprotocol checkout. */
static bool rrclient_server_uuid_valid(const char *uuid) {
   if (!uuid || strlen(uuid) != 36) {
      return false;
   }

   for (size_t i = 0 ; i < 36 ; ++i) {
      if (i == 8 || i == 13 || i == 18 || i == 23) {
         if (uuid[i] != '-') {
            return false;
         }
      } else if (!isxdigit((unsigned char)uuid[i])) {
         return false;
      }
   }

   return true;
}
bool rrclient_connection_expected_node_matches(rrconn_t *connection, const char *station, const char *domain) {
   if (!connection) return false;
   for (client_server_t *server = servers; server; server = server->next) {
      if (server->connection != connection) continue;
      const char *expected = get_server_property(server->name, "server.node-name");
      if (!expected || !*expected) return true;
      const char *dot = strchr(expected, '.');
      bool matches = dot && station && *station && domain && *domain &&
         (size_t)(dot - expected) == strlen(station) &&
         !g_ascii_strncasecmp(expected, station, strlen(station)) &&
         !g_ascii_strcasecmp(dot + 1, domain);
      if (!matches) {
         Log(LOG_WARN, "client.identity", "Node identity mismatch for %s: expected %s, announced station=%s domain=%s",
            server->name, expected, station ? station : "(missing)", domain ? domain : "(missing)");
         if (connection->native_transport)
            rr_transport_close(connection->native_transport);
      }
      return matches;
   }
   return false;
}
void rrclient_connection_set_identity(rrconn_t *connection, const char *domain, const char *server_uuid) {
   if (!connection || !domain || !*domain || !server_uuid || !rrclient_server_uuid_valid(server_uuid)) {
      return;
   }

   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->connection != connection) {
         continue;
      }

      if (strlen(domain) >= sizeof(s->domain)) {
         return;
      }

      for (const unsigned char *p = (const unsigned char *)domain ; *p ; ++p) {
         if (!(g_ascii_isalnum(*p) || *p == '.' || *p == '-' || *p == '_')) {
            return;
         }
      }

      if (s->domain[0] && (g_ascii_strcasecmp(s->domain, domain) || g_ascii_strcasecmp(s->server_uuid, server_uuid))) {
         Log(LOG_WARN, "connman", "Server %s attempted to change its domain identity", s->name);

         return;
      }
      g_strlcpy(s->domain, domain, sizeof(s->domain));

      for (char *p = s->domain ; *p ; ++p) {
         *p = g_ascii_tolower(*p);
      }

      g_strlcpy(s->server_uuid, server_uuid, sizeof(s->server_uuid));
      Log(LOG_INFO, "connman", "Server %s identified as %s in domain %s", s->name, s->server_uuid, s->domain);

      return;
   }
}
const char *rrclient_connection_domain(const rrconn_t *connection) {
   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->connection == connection) {
         return s->domain[0] ? s->domain : NULL;
      }
   }

   return NULL;
}
const char *rrclient_connection_server_uuid(const rrconn_t *connection) {
   for (client_server_t *s = servers ; s ; s = s->next) {
      if (s->connection == connection) {
         return s->server_uuid[0] ? s->server_uuid : NULL;
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
   if (!s) return 0;

   /* The picker must treat a scheduled retry as an in-progress connection,
    * even after CLOSE has set connected/ws_connected to zero. A second click
    * then invokes disconnect_server(), cancelling the backoff timer. */
   if (s->reconnect_pending && s->reconnect_enabled) return -1;
   return s == context ? ws_connected : s->connected;
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
      if (connection && s->connection == connection) {
         target = s;
         break;
      }
   }

   if (!target && connection) {
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

      if (ui_mode == UI_MODE_TUI) {
         tui_redraw_request();
      }
   }
   event_emit("client.server.selected", s->connection, s->name);

   return true;
}

bool rrclient_connection_cycle_status(bool status_active) {
   return status_active && rrclient_connection_cycle();
}

bool rrclient_connection_cycle(void) {
   if (!servers) {
      return false;
   }
   client_server_t *next = selected && selected->next ? selected->next : servers;
   client_server_t *first = next;
   do {
      /* 'connected' is the transport-independent online state, updated by both native TLS and legacy transports. */
      if (next->connected == 1) {
         return rrclient_connection_select(next->name);
      }
      next = next->next ? next->next : servers;
   } while (next != first);
   return false;
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

bool rrclient_window_is_shared(const char *window) {
   return window && (!strcasecmp(window, "status") || !strcasecmp(window, "client log") || !strcasecmp(window, "host log"));
}

const char *rrclient_window_name(const char *room) {
   if (!room || !*room || rrclient_window_is_shared(room) || !context) {
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

/* Lookup only: never create a window for a rejected JOIN or a numeric. Match the originating connection, not the currently selected server. */
const char *rrclient_window_find_for_connection(const rrconn_t *connection, const char *room) {
   if (!connection || !room || !*room) {
      return NULL;
   }

   for (client_window_t *w = windows ; w ; w = w->next) {
      if (w->server && w->server->connection == connection && window_room_equal(w, room)) {
         return w->key;
      }
   }

   return NULL;
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
/* The native server may close immediately after auth rejection, before its
 * auth.error frame reaches the client. Report the failed login once, from
 * either the explicit error or a disconnect while awaiting authorization. */
static void auth_failure_report(client_server_t *s, const char *reason) {
   if (!s) return;
   s->login_challenge_pending = false;
   cancel_reconnect(s);
   rrclient_auth_failure_alert_t alert = {0};
   g_strlcpy(alert.profile, s->name ? s->name : "(unknown)", sizeof(alert.profile));
   g_strlcpy(alert.reason, reason && *reason ? reason :
      "Login rejected or connection closed during authentication. Check your username/password.",
      sizeof(alert.reason));
   Log(LOG_WARN, "auth.ws", "Authentication failed for profile %s; automatic reconnection disabled: %s",
      alert.profile, alert.reason);
   ui_print(NULL, "%sAuthentication failed for %s: %s (automatic reconnect stopped)",
      get_chat_ts(now), alert.profile, alert.reason);
   event_emit_message(&(rr_event_message_t) {
      .name = RRCLIENT_AUTH_FAILURE_ALERT_EVENT, .sender = "core",
      .payload = &alert, .length = sizeof(alert)
   });
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

   /* The sign-on dialog is only for RustyRig WebSocket sessions, never IRC. */
   const char *url = get_server_property(s->name, "server.url");
   bool irc = (url && (!strncasecmp(url, "irc:", 4) || !strncasecmp(url, "ircs:", 5))) || (cptr && !cptr->is_ws);

   if (!irc) {
      if (!strcmp(event, "connecting")) {
         rrclient_connect_progress(s->name, RR_CONNECT_PHASE_LOOKUP);
      } else if (!strcmp(event, "connected")) {
         rrclient_connect_progress(s->name, RR_CONNECT_PHASE_TRANSPORT);
      } else if (!strcmp(event, "authorized")) {
         rrclient_connect_progress(s->name, RR_CONNECT_PHASE_AUTHORIZED);
      } else if (!strcmp(event, "disconnected") || !strcmp(event, "http.error") || !strcmp(event, "auth.error") || !strcmp(event, "error")) {
         rrclient_connect_progress(s->name, RR_CONNECT_PHASE_FAILED);
      }
   }

   /* These milestones are not reconnection failures. */
   if (!strcmp(event, "connecting") || !strcmp(event, "connected")) {
      return;
   }

   if (!strcmp(event, "authorized") || !strcmp(event, "irc.connected") || !strcmp(event, "disconnected")) {
      nick_prompt_close(s);
      s->nick_recovering = false;
   }

   if (!strcmp(event, "auth.error")) {
      const char *reason = NULL;
      dict *detail = data ? json2dict(data) : NULL;
      if (detail) reason = dict_get(detail, "auth.error", NULL);
      auth_failure_report(s, reason && *reason ? reason : "Server rejected authentication. Check username/password.");
      if (detail) dict_free(detail);
   } else if (!strcmp(event, "authorized") || !strcmp(event, "irc.connected")) {
      s->login_challenge_pending = false;
      s->reconnect_pending = false;
      s->reconnect_tries = 0;
      ws_connected = 1;
   } else if (!strcmp(event, "logging-in") && cptr && cptr->is_ws) {
      s->login_challenge_pending = true;
   } else if (!strcmp(event, "disconnected") && s->login_challenge_pending && cptr && cptr->is_ws) {
      auth_failure_report(s, NULL);
   } else if ((!strcmp(event, "disconnected") || !strcmp(event, "http.error")) &&
              s->reconnect_enabled && !s->reconnect_pending && !dying) {
      /* A protocol 'error' is not a transport failure. In particular,
       * password-change-required errors must not tear down an authenticated
       * session while the user is being prompted to rotate credentials. */
      if (s->reconnect_tries >= RRC_MAX_RECONNECTS) {
         cancel_reconnect(s);
         ui_print(NULL, "Giving up after %u reconnect attempts", RRC_MAX_RECONNECTS);

         return;
      }
      s->reconnect_tries++;
      /* PARITY: rustyrig-www/js/webui.js exponential reconnect base interval precedence. */
      const char *override = get_server_property(s->name, "server.reconnect-interval");
      char *end = NULL;
      long interval = override && *override ? strtol(override, &end, 10) : cfg_get_int("server.reconnect-interval", 1);

      if (interval < 1 || interval >= 60 || (end && *end)) {
         interval = 1;
      }

      for (unsigned i = 1 ; i < s->reconnect_tries && interval < 60 ; i++) {
         interval *= 2;
      }

      if (interval > 60) {
         interval = 60;
      }
      s->reconnect_pending = true;
      s->reconnect_at = now + interval;
      ws_connected = -1;
      ui_print(NULL, "Reconnecting in %ld seconds (attempt %u/%u)", interval, s->reconnect_tries, RRC_MAX_RECONNECTS);
   }
}

static void connecting_cancel_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_connect_progress_t)) {
      return;
   }
   rr_connect_progress_t request;
   memcpy(&request, message->payload, sizeof(request));

   if (!memchr(request.profile, 0, sizeof(request.profile)) || !request.profile[0]) {
      return;
   }
   disconnect_server(request.profile);
}

/* The core validates a locally confirmed, previously observed key before
 * replacing the profile's saved pin and scheduling another TLS handshake. */
static void tls_pin_reply_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
#ifndef USE_MONGOOSE
   if (!message || !message->payload || message->length != sizeof(rrclient_tls_pin_request_t))
      return;
   rrclient_tls_pin_request_t request;
   memcpy(&request, message->payload, sizeof(request));
   if (!memchr(request.profile, 0, sizeof(request.profile)) ||
       !memchr(request.old_pin, 0, sizeof(request.old_pin)) ||
       !memchr(request.new_pin, 0, sizeof(request.new_pin))) return;
   client_server_t *server = find_server(request.profile);
   if (!server || !server->pending_tls_pin_confirmation ||
       strcmp(server->pending_tls_pin, request.new_pin)) return;
   const char *current = get_server_property(server->name, "server.tls-spki-sha256");
   /* An unseen key has no previously stored pin: an absent property and
    * an empty string are equivalent for the first-use confirmation. */
   if (strcmp(current ? current : "", request.old_pin)) return;
   server->pending_tls_pin_confirmation = false;
   server->pending_tls_pin[0] = '\0';
   if (request.decision == 0 || request.decision > 2) {
      server->temporary_tls_pin[0] = '\0';
      Log(LOG_INFO, "connman", "TLS trust denied for %s", server->name);
      return;
   }
   if (request.decision == 2) {
      if (!rrclient_database_server_set(server->name, "server.tls-spki-sha256", request.new_pin)) {
         Log(LOG_WARN, "connman", "Could not persist accepted TLS pin for profile %s; keeping connection blocked", server->name);
         return;
      }
      server->temporary_tls_pin[0] = '\0';
      Log(LOG_AUDIT, "connman", "Persistently trusted TLS public key for %s", server->name);
   } else {
      g_strlcpy(server->temporary_tls_pin, request.new_pin, sizeof(server->temporary_tls_pin));
      Log(LOG_AUDIT, "connman", "Trusted TLS public key once for %s", server->name);
   }
   rr_server_url_t approved_endpoint;
   if (!server->url || !rr_server_url_parse(server->url, &approved_endpoint) || approved_endpoint.irc || !approved_endpoint.tls) {
      Log(LOG_WARN, "connman", "Trust approved for %s, but the current profile URL is not a RustyRig WSS endpoint; reconnect refused", server->name);
      server->temporary_tls_pin[0] = '\0';
      return;
   }
   Log(LOG_INFO, "connman", "TLS trust approved for %s; scheduling WSS reconnect to %s:%u",
      server->name, approved_endpoint.host, approved_endpoint.port);
   server->reconnect_enabled = true;
   server->reconnect_pending = true;
   server->reconnect_at = now;
#endif
}
static void password_secret_clear(client_server_t *s) {
   if (s && s->pending_password_change) {
      explicit_bzero(s->pending_password_change, strlen(s->pending_password_change));
      free(s->pending_password_change);
      s->pending_password_change = NULL;
   }
}
static void password_change_result(client_server_t *s, bool success, const char *reason) {
   rrclient_password_change_t reply = {0};
   g_strlcpy(reply.profile, s->name, sizeof(reply.profile));
   if (s->connection && s->connection->server)
      g_strlcpy(reply.username, s->connection->server->nick, sizeof(reply.username));
   reply.success = success;
   g_strlcpy(reply.reason, reason ? reason : "", sizeof(reply.reason));
   event_emit_message(&(rr_event_message_t) {
      .name = RRCLIENT_PASSWORD_RESULT_EVENT, .sender = "core",
      .payload = &reply, .length = sizeof(reply)
   });
}
static void password_change_required(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)event; (void)data; (void)user;
   client_server_t *s = context;
   if (!s || !conn || s->connection != conn || !conn->authenticated ||
       !conn->is_ws || s->password_prompt_open) return;
   rrclient_password_change_t request = {0};
   g_strlcpy(request.profile, s->name, sizeof(request.profile));
   g_strlcpy(request.username, conn->server ? conn->server->nick : "", sizeof(request.username));
   /* The username is the account from the connected server, never an
    * arbitrary user-controlled target in the password rotation request. */
   s->password_prompt_open = true;
   event_emit_message(&(rr_event_message_t) {
      .name = RRCLIENT_PASSWORD_PROMPT_EVENT, .sender = "core",
      .payload = &request, .length = sizeof(request)
   });
}
static void password_change_retry(const rr_event_message_t *msg, void *ignored) {
   (void)ignored;
   if (!msg || !msg->payload || msg->length != sizeof(rrclient_password_change_t)) return;
   rrclient_password_change_t request;
   memcpy(&request, msg->payload, sizeof(request));
   if (!memchr(request.profile, 0, sizeof(request.profile))) return;
   client_server_t *s = find_server(request.profile);
   if (!s || !s->connection || !s->connection->authenticated ||
       !s->connection->is_ws || s->pending_password_change) return;
   rrclient_password_change_t prompt = {0};
   g_strlcpy(prompt.profile, s->name, sizeof(prompt.profile));
   if (s->connection->server)
      g_strlcpy(prompt.username, s->connection->server->nick, sizeof(prompt.username));
   s->password_prompt_open = true;
   event_emit_message(&(rr_event_message_t) {
      .name = RRCLIENT_PASSWORD_PROMPT_EVENT, .sender = "core",
      .payload = &prompt, .length = sizeof(prompt)
   });
}
static void password_change_submit(const rr_event_message_t *msg, void *ignored) {
   (void)ignored;
   if (!msg || !msg->payload || msg->length != sizeof(rrclient_password_change_t)) return;
   rrclient_password_change_t request;
   memcpy(&request, msg->payload, sizeof(request));
   if (!memchr(request.profile, 0, sizeof(request.profile)) ||
       !memchr(request.username, 0, sizeof(request.username)) ||
       !memchr(request.password, 0, sizeof(request.password))) goto done;
   client_server_t *s = find_server(request.profile);
   if (!s || !s->connection || !s->connection->authenticated ||
       !s->connection->is_ws || !s->password_prompt_open) goto done;
   if (!request.password[0]) {
      s->password_prompt_open = false; /* user cancelled; allow later manual retry */
      goto done;
   }
   size_t n = strlen(request.password);
   if (n < 8 || n > 256 || s->pending_password_change) {
      password_change_result(s, false, "Password must be 8-256 characters; retry the operation.");
      goto done;
   }
   rrconn_t *conn = s->connection;
   const char *account = conn->server ? conn->server->nick : "";
   bool verified_tls = conn->native_transport &&
      (rr_transport_capabilities(conn->native_transport) & RR_TRANSPORT_TLS_AUTHENTICATED);
#ifdef USE_MONGOOSE
   if (conn->conn && conn->conn->is_tls) verified_tls = true;
#endif
   if (!*account || strcmp(request.username, account) || !conn->token[0] || !verified_tls) {
      password_change_result(s, false, "Authenticated TLS session or account identity unavailable.");
      goto done;
   }
   s->pending_password_change = strdup(request.password);
   if (!s->pending_password_change) {
      password_change_result(s, false, "Unable to allocate password change request.");
      goto done;
   }
   dict *out = dict_new();
   bool sent = false;
   if (out) {
      dict_add(out, "msg.type", "auth");
      dict_add(out, "auth.cmd", "change-password");
      dict_add(out, "auth.user", account);
      dict_add(out, "auth.token", conn->token);
      dict_add(out, "auth.new-password", request.password);
      sent = ws_send_dict(NULL, conn, out, WEBSOCKET_OP_TEXT);
      dict_free(out);
   }
   if (!sent) {
      password_secret_clear(s);
      password_change_result(s, false, "Cannot send password change over the verified TLS connection.");
   } else {
      Log(LOG_INFO, "auth.ws", "Password change submitted for %s; waiting for server confirmation", s->name);
   }
done:
   explicit_bzero(&request, sizeof(request));
}
static void password_change_response(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)user;
   client_server_t *s = context;
   if (!s || s->connection != conn || !s->pending_password_change) return;
   bool success = !strcmp(event, "client.password-change.accepted");
   dict *response = data ? json2dict(data) : NULL;
   const char *reason = response ? dict_get(response, "auth.reason", NULL) : NULL;
   if (success) {
      if (!rrclient_database_server_set(s->name, "server.pass", s->pending_password_change)) {
         password_change_result(s, false, "Server changed the password, but saving it locally failed. Update the server profile manually.");
         password_secret_clear(s);
         s->password_prompt_open = false;
         if (response) dict_free(response);
         return;
      }
      Log(LOG_INFO, "auth.ws", "Password changed and saved for %s; reconnecting", s->name);
      password_change_result(s, true, "");
      password_secret_clear(s);
      s->password_prompt_open = false;
      s->reconnect_enabled = true;
      s->reconnect_pending = true;
      s->reconnect_at = now;
      if (conn->native_transport) rr_transport_close(conn->native_transport);
#ifdef USE_MONGOOSE
      else if (conn->conn) conn->conn->is_closing = 1;
#endif
   } else {
      password_secret_clear(s);
      s->password_prompt_open = false;
      password_change_result(s, false, reason ? reason : "Server rejected the new password.");
   }
   if (response) dict_free(response);
}
void connman_register_events(void) {
   event_on_message_domain_token(RRCLIENT_TLS_PIN_REPLY_EVENT, tls_pin_reply_event, NULL, "core");
   event_on_message_domain_token(RRCLIENT_PASSWORD_SUBMIT_EVENT, password_change_submit, NULL, "core");
   event_on_message_domain_token(RRCLIENT_PASSWORD_RETRY_EVENT, password_change_retry, NULL, "core");
   event_on("client.password-change.required", password_change_required, NULL);
   event_on("client.password-change.accepted", password_change_response, NULL);
   event_on("client.password-change.failed", password_change_response, NULL);
   event_on_message_domain_token("client.connecting.cancel", connecting_cancel_event, NULL, "core");
   event_on("connecting", connection_event, NULL);
   event_on("connected", connection_event, NULL);
   event_on("irc.nick-in-use", nick_in_use, NULL);
   event_on("irc.nick", nick_accepted, NULL);
   event_on("authorized", connection_event, NULL);
   event_on("irc.connected", connection_event, NULL);
   event_on("disconnected", connection_event, NULL);
   event_on("http.error", connection_event, NULL);
   event_on("auth.error", connection_event, NULL);
   event_on("logging-in", connection_event, NULL);
}

static uint64_t control_rx_bytes, control_tx_bytes, ws_text_rx_bytes;
void rrclient_ws_bytes(uint64_t *rx, uint64_t *tx, uint64_t *text_rx) {
   if (rx) {
      *rx = control_rx_bytes;
   }

   if (tx) {
      *tx = control_tx_bytes;
   }

   if (text_rx) {
      *text_rx = ws_text_rx_bytes;
   }
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

   /* Mongoose supplies a pointer to the transferred byte count for READ and WRITE. Count at the transport edge, before unselected connections can return early.
    * No per-event logs or profiling allocations. */
   if (data && (ev == MG_EV_READ || ev == MG_EV_WRITE)) {
      long bytes = *(long *)data;

      if (bytes > 0) {
         if (ev == MG_EV_READ) {
            control_rx_bytes += (uint64_t) bytes;
         } else {
            control_tx_bytes += (uint64_t) bytes;
         }
      }
   }

   if (s->connection != cptr) {
      if (ev == MG_EV_CLOSE) {
         irc_capabilities_clear(cptr);
         mg_iobuf_free(&t->websocket_request);

         if (cptr->native_transport) {
            rr_transport_mongoose_closed(cptr->native_transport);
            rr_transport_mongoose_release(cptr->native_transport);
            cptr->native_transport = NULL;
         }
         free(t);
         c->fn_data = NULL;
      }

      return;
   }

   /* Count only decoded TEXT messages as control payload. Binary messages can carry audio or other media and must never be called control traffic. */
   if (ev == MG_EV_WS_MSG && data && cptr->is_ws) {
      const struct mg_ws_message *frame = data;

      if ((frame->flags & 15) == WEBSOCKET_OP_TEXT) {
         ws_text_rx_bytes += frame->data.len;
      }
   }

   /* Background audio keeps decoding with muted output. Serial output remains selected-server-only. */
   if (ev == MG_EV_WS_MSG && s != selected && data && ((((struct mg_ws_message *)data)->flags & 15) == WEBSOCKET_OP_BINARY)) {
      struct mg_ws_message *message = data;
      struct rr_binframe frame;

      if (rr_binframe_parse((const uint8_t *)message->data.buf, message->data.len, &frame) || frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
         return;
      }
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

   if (ev == MG_EV_WS_OPEN && cptr->is_ws && !cptr->native_transport) {
      /* The upgrade is complete: only now does the WebSocket adapter become writable. The Mongoose socket remains owned by mg_mgr. */
      cptr->native_transport = rr_transport_mongoose_adopt(c, NULL, NULL);

      if (cptr->native_transport) {
         rr_transport_mongoose_open(cptr->native_transport);
      }
   }

   if (cptr->is_ws) {
      http_handler(c, ev, data);
   } else {
      irc_mongoose_handler(c, ev, data);
   }
   dispatch_depth--;

   if (ev == MG_EV_CLOSE) {
      s->domain[0] = '\0';
      s->server_uuid[0] = '\0';
      mg_iobuf_free(&t->websocket_request);

      if (cptr->native_transport) {
         rr_transport_mongoose_closed(cptr->native_transport);
         rr_transport_mongoose_release(cptr->native_transport);
         cptr->native_transport = NULL;
      }
      cptr->conn = NULL;
      /* Keep the offline profile selectable, but never retain a dead socket. */
   }
   s->connected = ws_connected;
   activate(dispatch_depth ? previous : selected);
}
#endif

#ifndef USE_MONGOOSE
struct rrclient_native_connection {
   rrconn_t connection;
   server_cfg_t server;
   rr_transport_t *transport;
   client_server_t *owner;
   bool closed;
   bool irc;
};

static void native_event(rr_transport_t *transport, enum rr_transport_event event, rr_transport_channel_t *channel, const uint8_t *data, size_t len, void *user)
{
   (void)channel;
   (void)transport;
   rrclient_native_connection_t *native = user;
   client_server_t *s = native->owner;

   if (s->native != native) {
      return;
   }
   rrconn_t *cptr = &native->connection;
   client_server_t *previous = context;
   activate(s);
   dispatch_depth++;

   if (event == RR_TRANSPORT_EVENT_OPEN && native->irc) {
      s->connected = ws_connected = cptr->connected = now;
      Log(LOG_INFO, "irc", "Native IRC connected to %s", s->name);
      irc_client_register(cptr);
   } else if (event == RR_TRANSPORT_EVENT_STREAM_DATA && native->irc) {
      irc_receive(cptr, data, len);
      rrclient_connection_count_traffic(cptr, len, 0);
   } else if (event == RR_TRANSPORT_EVENT_OPEN) {
      s->connected = ws_connected = -1;
      Log(LOG_INFO, "connman", "Native WebSocket connected to %s", s->name);
      dict *info = dict_new();

      if (info) {
         dict_add(info, "connected.server", s->name);
         event_emit_dict("connected", cptr, info);
         dict_free(info);
      }
      dict *cap = dict_new();

      if (cap) {
         dict_add(cap, "msg.type", "cap");
         dict_add(cap, "cap.cmd", "req");
         dict_add_int(cap, "cap.version", RR_PROTOCOL_VERSION);
         dict_add(cap, "cap.features", "wire.json-v2 binframe.v2 object.inventory-v2");
         ws_send_dict(NULL, cptr, cap, WEBSOCKET_OP_TEXT);
         dict_free(cap);
      }
   } else if (event == RR_TRANSPORT_EVENT_MESSAGE) {
      rr_traffic_count(cptr, false, false, len);
      rrclient_native_text_receive(cptr, data, len);
   } else if (event == RR_TRANSPORT_EVENT_BINARY_MESSAGE) {
      rr_traffic_count(cptr, false, true, len);
      ws_binframe_process_client(cptr, (const char *)data, len);
   } else if (event == RR_TRANSPORT_EVENT_ERROR && native->irc) {
      Log(LOG_WARN, "irc", "IRC %s: %.*s",s->name,(int)MIN(len,(size_t)240),data?(const char *)data:"");
   } else if (event == RR_TRANSPORT_EVENT_ERROR) {
      const char mismatch[] = "TLS_PIN_MISMATCH:";
      const char unknown[] = "TLS_TRUST_REQUIRED:";
      bool changed = data && len == sizeof(mismatch)-1+64 &&
         !memcmp(data, mismatch, sizeof(mismatch)-1);
      bool first = data && len == sizeof(unknown)-1+64 &&
         !memcmp(data, unknown, sizeof(unknown)-1);
      if (changed || first) {
         rrclient_tls_pin_request_t request = {0};
         const char *previous = get_server_property(s->name, "server.tls-spki-sha256");
         const char *enabled = get_server_property(s->name, "server.tls-trust-on-first-use");
         bool tofu = !enabled || (!g_ascii_strcasecmp(enabled, "true") || !strcmp(enabled, "1"));
         const char *presented = (const char *)data +
            (changed ? sizeof(mismatch)-1 : sizeof(unknown)-1);
         if (tofu && strlen(s->name) < sizeof(request.profile) &&
             strspn(presented, "0123456789abcdef") >= 64 && !s->pending_tls_pin_confirmation) {
            g_strlcpy(request.profile, s->name, sizeof(request.profile));
            if (previous) g_strlcpy(request.old_pin, previous, sizeof(request.old_pin));
            memcpy(request.new_pin, presented, 64);
            const char *cn = rr_transport_wss_peer_common_name(transport);
            const char *sans = rr_transport_wss_peer_dns_names(transport);
            const char *cert = rr_transport_wss_peer_cert_sha256(transport);
            const char *node = get_server_property(s->name, "server.node-name");
            if (cn) g_strlcpy(request.cert_cn, cn, sizeof(request.cert_cn));
            if (sans) g_strlcpy(request.cert_sans, sans, sizeof(request.cert_sans));
            if (cert) g_strlcpy(request.cert_sha256, cert, sizeof(request.cert_sha256));
            if (node) g_strlcpy(request.node_name, node, sizeof(request.node_name));
            if (s->url) g_strlcpy(request.address, s->url, sizeof(request.address));
            g_strlcpy(s->pending_tls_pin, request.new_pin, sizeof(s->pending_tls_pin));
            s->pending_tls_pin_confirmation = true;
            s->reconnect_pending = false;
            s->reconnect_enabled = false;
            Log(LOG_WARN, "connman", "TLS trust decision required for %s (%s key)", s->name, changed ? "changed" : "unknown");
            event_emit_message(&(rr_event_message_t) {
               .name = RRCLIENT_TLS_PIN_PROMPT_EVENT,
               .payload = &request, .length = sizeof(request)
            });
         }
      } else {
         Log(LOG_WARN, "connman", "Native connection to %s: %.*s", s->name,
            (int)MIN(len, (size_t)240), data ? (const char *)data : "");
      }
      event_emit("http.error", cptr, NULL);
   } else if (event == RR_TRANSPORT_EVENT_CLOSE) {
      if (s->pending_password_change) {
         password_secret_clear(s);
         s->password_prompt_open = false;
         cancel_reconnect(s); /* The server may have changed it; don't retry the old password. */
         password_change_result(s, false, "Disconnected before the server confirmed the password change. Your saved password was not modified.");
      }
      /* Do not rely on the deferred disconnected event: it may run after
       * the connection context has been detached, and auth.error may never
       * arrive if the peer closed immediately after sending it. */
      if (s->login_challenge_pending && !cptr->authenticated)
         auth_failure_report(s, NULL);
      native->closed = true;
      if (native->irc) {
         cptr->connected = 0;
         cptr->authenticated = cptr->sent_login = false;
         irc_capabilities_clear(cptr);
         event_emit("irc.disconnected",cptr,"");
      }
      s->connected = ws_connected = 0;
      s->domain[0] = s->server_uuid[0] = 0;
      dict *info = dict_new();

      if (info) {
         dict_add(info, "msg.type", "auth");
         dict_add(info, "auth.server", s->name);
         event_emit_dict("disconnected", cptr, info);
         dict_free(info);
      }
   }
   s->connected = ws_connected;
   dispatch_depth--;
   activate(dispatch_depth ? previous : selected);
}
#endif

void rrclient_poll_events_reconnect(void) {
#ifndef USE_MONGOOSE

   for (client_server_t *s = servers ; s ; s = s->next) {
      rrclient_native_connection_t *native = s->native;

      if (native && native->transport) {
         rr_transport_poll(native->transport, 0);

         if (native->closed || rr_transport_state(native->transport) == RR_TRANSPORT_CLOSED) {
            if (native->irc) rr_transport_tcp_release(native->transport);
            else rr_transport_wss_release(native->transport);
            native->connection.native_transport = NULL;
            native->transport = NULL;
            s->native = NULL;

            if (s->connection == &native->connection) {
               /* The selected profile may still be the active context;
                * rebuild its borrowed pointers before releasing the session. */
               bool active = context == s;

               if (active) {
                  activate(NULL);
               }
               s->connection = NULL;

               if (active) {
                  activate(s);
               }
            }
            free(native);
         }
      }

      if (s->reconnect_pending && now >= s->reconnect_at && !dying && !s->native) {
         s->reconnect_pending = false;

         if (s->reconnect_enabled) {
            reconnect_attempting = true;
            rrclient_connect_url(s->name, s->url);
            reconnect_attempting = false;
         }
      }
   }

#else

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
#ifndef USE_MONGOOSE

   if (s->native && s->native->transport) {
      rr_transport_close(s->native->transport);
   }
#else

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

   /* Emit at the actual connection attempt, not a transport event which may never be delivered. IRC profiles deliberately do not show this dialog. */
   if (!endpoint.irc) {
      rrclient_connect_progress(s->name, RR_CONNECT_PHASE_LOOKUP);
   }
   ui_print(NULL, "%sConnecting to %s", get_chat_ts(now), endpoint.credentials ? connect_url : url);
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

   /* Both IRC and WSS use the fail-closed SOCKS5 dialer. */
   const char *proxy=profile ? get_server_property(profile,"server.proxy") : NULL;
   if (!proxy || !*proxy) proxy=cfg_get("server.proxy");
   if (proxy_override) {
      if (!*proxy_override) return true;
      proxy=proxy_override;
   } else if (profile) {
      client_server_t *existing=find_server(profile);
      if (existing && existing->native_proxy_override) proxy=existing->native_proxy_override;
   }
   const char *proxy_user=profile ? get_server_property(profile,"server.proxy.user") : NULL;
   const char *proxy_pass=profile ? get_server_property(profile,"server.proxy.pass") : NULL;
   if (!proxy_user) proxy_user=cfg_get("server.proxy.user");
   if (!proxy_pass) proxy_pass=cfg_get("server.proxy.pass");
   if (proxy && *proxy && strncmp(proxy,"socks5://",9) && strncmp(proxy,"socks5h://",10)) {
      ui_print(NULL,"Invalid SOCKS5 proxy URL");return true;
   }
   if (!profile || !*profile) {
      profile = endpoint.host;
   }
   client_server_t *s = find_server(profile);

   if (s && s->native) {
      rrclient_connection_select(s->name);

      return false;
   }

   if (!s) {
      s = calloc(1, sizeof(*s));

      if (!s || !(s->name = strdup(profile))) {
         free(s);

         return true;
      }
      client_server_t **tail = &servers;

      while (*tail) {
         tail = &(*tail)->next;
      }
      *tail = s;
   }
   char *saved_url = strdup(url);

   if (!saved_url) {
      return true;
   }
   if (proxy_override) {
      char *saved_proxy=strdup(proxy_override);
      if (!saved_proxy) {free(saved_url);return true;}
      free(s->native_proxy_override);
      s->native_proxy_override=saved_proxy;
   }
   free(s->url);
   s->url = saved_url;

   /* endpoint.path borrows storage from the URL; reparse after replacing the saved profile URL, especially on reconnect. */
   if (!rr_server_url_parse(s->url, &endpoint)) {
      return true;
   }
   rrclient_native_connection_t *native = calloc(1, sizeof(*native));

   if (!native) {
      return true;
   }
   native->owner = s;
   native->connection.fd = -1;
   native->irc = endpoint.irc;
   native->connection.is_ws = !endpoint.irc;
   native->connection.server = &native->server;
   g_strlcpy(native->server.network, s->name, sizeof(native->server.network));
   g_strlcpy(native->server.host, endpoint.host, sizeof(native->server.host));
   native->server.port = endpoint.port;
   native->server.tls = endpoint.tls;
   const char *nick = get_server_property(s->name, "server.user");
   const char *password = get_server_property(s->name, "server.pass");

   if (endpoint.credentials) {
      nick = endpoint.user;
      password = endpoint.pass;
   }

   if ((!nick || !*nick) && endpoint.irc) nick=cfg_get("irc.nick");
   if (!nick || !*nick) nick=endpoint.irc ? "nonick" : "guest";

   if (strlen(nick) >= sizeof(native->server.nick) || (password && strlen(password) >= sizeof(native->server.pass))) {
      free(native);

      return true;
   }
   g_strlcpy(native->connection.nick, nick, sizeof(native->connection.nick));
   g_strlcpy(native->server.nick, nick, sizeof(native->server.nick));
   g_strlcpy(native->server.pass, password ? password : "", sizeof(native->server.pass));
   const char *ca = get_server_property(s->name, "server.tls-ca");

   if (!ca || !*ca) {
      ca = cfg_get("tls.ca-file");
   }
   /* The logical TLS name is independent of the URL's dial address. Require an explicit per-profile hostname when dialing by IP/alias;
    * never substitute a claimed peer identity after the TLS handshake. */
   const char *tls_name = get_server_property(s->name, "server.node-name");
   if (!tls_name || !*tls_name) tls_name = get_server_property(s->name, "server.tls-name");
   const char *tofu_value = get_server_property(s->name, "server.tls-trust-on-first-use");
   bool tofu = !tofu_value || (!g_ascii_strcasecmp(tofu_value, "true") || !strcmp(tofu_value, "1"));
   const char *pin = tofu ? (s->temporary_tls_pin[0] ? s->temporary_tls_pin :
      get_server_property(s->name, "server.tls-spki-sha256")) : NULL;
   Log(LOG_INFO, "connman", "Native WSS profile=%s endpoint=%s:%u trust-mode=%s pin=%s expected-node=%s",
      s->name, endpoint.host, endpoint.port, !tofu ? "CA/hostname" : "SPKI/approval",
      pin && *pin ? "present" : "unapproved", tls_name && *tls_name ? tls_name : "(unspecified)");
   rr_wss_client_options_secure_t opts = {
      .user_agent = "RustyRig/1.0",
      .subprotocol = RR_WS_SUBPROTOCOL,
      .ca_file = ca,
      .tls_server_name = tls_name && *tls_name ? tls_name : NULL,
      .tls_peer_spki_sha256 = pin && *pin ? pin : NULL,
      .tls_trust_on_first_use = tofu && (!pin || !*pin)
   };
   char connect_url[2048];
   snprintf(connect_url, sizeof(connect_url), "%s://%s%s%s:%u%s", endpoint.irc ? (endpoint.tls ? "ircs" : "irc") : (endpoint.tls ? "wss" : "ws"),
      endpoint.ipv6 ? "[" : "", endpoint.host, endpoint.ipv6 ? "]" : "", endpoint.port,
      endpoint.irc ? "" : (!strcmp(endpoint.path, "/") ? "/ws" : endpoint.path));
   if (endpoint.irc) {
      const char *sasl_user=get_server_property(s->name,"server.sasl.user");
      const char *sasl_pass=get_server_property(s->name,"server.sasl.pass");
      const char *mechanism=get_server_property(s->name,"server.sasl.mechanism");
      if (sasl_user) g_strlcpy(native->server.sasl_user,sasl_user,sizeof(native->server.sasl_user));
      if (sasl_pass) g_strlcpy(native->server.sasl_pass,sasl_pass,sizeof(native->server.sasl_pass));
      g_strlcpy(native->server.sasl_mechanism,mechanism && *mechanism ? mechanism : "PLAIN",
                sizeof(native->server.sasl_mechanism));
      rr_tcp_client_options_t tcp={.host=endpoint.host,.port=endpoint.port,.tls=endpoint.tls,
         .ca_file=ca,.tls_server_name=tls_name,.proxy_url=proxy,
         .proxy_user=proxy_user,.proxy_pass=proxy_pass};
      native->transport=rr_transport_tcp_connect(&tcp,native_event,native);
   } else {
      rr_wss_client_options_proxy_t proxied={.secure=opts,.proxy_url=proxy,
         .proxy_user=proxy_user,.proxy_pass=proxy_pass};
      native->transport=rr_transport_wss_connect_proxy(connect_url,&proxied,native_event,native);
   }
   /* A temporary approval authorizes this single connection attempt only. */
   s->temporary_tls_pin[0] = '\0';

   if (!native->transport) {
      ui_print(NULL, "Unable to initialize native WebSocket connection for %s", s->name);
      free(native);

      return true;
   }
   native->connection.native_transport = native->transport;
   s->connection = &native->connection;
   s->native = native;
   s->connected = -1;
   s->login_challenge_pending = false;
   s->reconnect_pending = false;
   s->reconnect_enabled = true;

   if (!reconnect_attempting) {
      s->reconnect_tries = 0;
   }

   if (!selected || !reconnect_attempting) {
      selected = s;
   }

   /* A reconnect may replace the transport while the same profile is selected; refresh the borrowed ws_conn/global context pointers. */
   if (context == s) {
      activate(NULL);
   }
   activate(s);
   if (!endpoint.irc) rrclient_connect_progress(s->name, RR_CONNECT_PHASE_LOOKUP);
   ui_print(NULL,"%sConnecting to %s via native %s%s",get_chat_ts(now),connect_url,
      endpoint.irc ? "IRC" : "WSS",proxy && *proxy ? " (SOCKS5)" : "");
   event_emit("client.server.selected", s->connection, s->name);

   return false;
#endif
}

bool connect_or_disconnect(const char *name) {
   client_server_t *s = find_server(name);
#ifdef USE_MONGOOSE

   if (s && ((s->connection && s->connection->conn && !s->connection->conn->is_closing) ||
       (s->reconnect_pending && s->reconnect_enabled))) {
      return disconnect_server(name);
   }
#else

   if (s && (s->native || (s->reconnect_pending && s->reconnect_enabled))) {
      return disconnect_server(name);
   }
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
   bool any = false;

   for (char *name = strtok_r(list, ", \t", &save) ; name ; name = strtok_r(NULL, ", \t", &save)) {
      const char *url = get_server_property(name, "server.url");

      if (!url || !*url) {
         continue;              /* Stale legacy config preference. */
      }
      any = true;
      connect_server(name);
   }

   free(list);

   if (!any) {
      show_server_chooser();
   }
}

void rrclient_quit_servers(const char *reason) {
#ifdef USE_MONGOOSE

   for (client_server_t *s = servers ; s ; s = s->next) {
      rrconn_t *c = s->connection;
      cancel_reconnect(s);

      if (c && c->is_ws && c->authenticated && c->conn && !c->conn->is_closing && !c->conn->is_draining) {
         dict *request = dict_new();

         if (request) {
            dict_add(request, "msg.type", "auth");
            dict_add(request, "auth.cmd", "quit");
            dict_add(request, "auth.msg", reason ? reason : "Client exiting");
            ws_send_dict(NULL, c, request, WEBSOCKET_OP_TEXT);
            dict_free(request);
         }
      }

      if (c && !c->is_ws && c->sent_login && c->conn && !c->conn->is_closing && !c->conn->is_draining) {
         if (!irc_send(c, "QUIT :%s", reason ? reason : "Client exiting")) {
            irc_send(c, "QUIT :Client exiting");
         }
         c->conn->is_draining = 1;
      }
   }

#else

   for (client_server_t *s = servers ; s ; s = s->next) {
      cancel_reconnect(s);

      if (s->native && s->native->transport) {
         if (s->native->irc) {
            if (s->connection && s->connection->sent_login)
               irc_send(s->connection, "QUIT :%s", reason ? reason : "Client exiting");
         } else if (s->connection && s->connection->authenticated) {
            dict *request = dict_new();

            if (request) {
               dict_add(request, "msg.type", "auth");
               dict_add(request, "auth.cmd", "quit");
               dict_add(request, "auth.msg", reason ? reason : "Client exiting");
               ws_send_dict(NULL, s->connection, request, WEBSOCKET_OP_TEXT);
               dict_free(request);
            }
         }
         rr_transport_close(s->native->transport);
      }
   }

#endif
}

void rrclient_flush_quit(void) {
#ifndef USE_MONGOOSE

   for (unsigned i = 0 ; i < 20 ; i++) {
      bool pending = false;

      for (client_server_t *s = servers ; s ; s = s->next) {
         if (s->native && s->native->transport) {
            rr_transport_poll(s->native->transport, 0);
            pending = pending || rr_transport_state(s->native->transport) != RR_TRANSPORT_CLOSED;
         }
      }

      if (!pending) {
         break;
      }
      g_usleep(10000);
   }

#else
   uint64_t deadline = mg_millis() + 1000;
   while (mg_millis() < deadline) {
      bool pending = false;

      for (client_server_t *s = servers ; s ; s = s->next) {
         rrconn_t *c = s->connection;

         if (c && c->conn && ((!c->is_ws && c->conn->is_draining) || (c->is_ws && c->conn->send.len))) {
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
      password_secret_clear(servers);
      free(servers->media);
      free(servers->wire_rooms);
      free((void *)servers->user);
#ifndef USE_MONGOOSE

      if (servers->native) {
         if (servers->native->transport) {
            rr_transport_close(servers->native->transport);
            rr_transport_poll(servers->native->transport, 0);

            if (rr_transport_state(servers->native->transport) == RR_TRANSPORT_CLOSED) {
               rr_transport_wss_release(servers->native->transport);
            }
         }
         free(servers->native);
         servers->connection = NULL;
      }
#endif
      free(servers->connection);
      free(servers->name);
      free(servers->url);
#ifndef USE_MONGOOSE
      free(servers->native_proxy_override);
#endif
#ifdef USE_MONGOOSE
      free(servers->proxy);
#endif
      free(servers);
      servers = next;
   }
}
