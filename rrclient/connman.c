//
// rrclient/connman.c: Connection Manager
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// XXX: This needs finished to fully support multiple connections in one client
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <librrprotocol/http.h>
#include <librrprotocol/irc.h>

// Server connections
extern int ws_connected;
extern const char *login_user;
extern rrconn_t *ws_conn, *ws_tx_conn;
extern rr_connection_t *active_connections;
extern dict *cfg;
extern bool dying;
extern time_t now, poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN + 1];
extern void rrclient_update_connection_ui(int connected);       // events.c
extern void tui_refresh_sb_online(void);                        // events.c

static const char *rrclient_resolve_server_name(const char *requested_server) {
   if (requested_server && *requested_server) {
      return requested_server;
   }
   const char *autoconnect = cfg_get_exp("server.auto-connect");

   if (autoconnect) {
      return autoconnect;
   }

   if (server_name && *server_name) {
      return server_name;
   }

   return NULL;
}

#ifdef  USE_MONGOOSE
extern struct mg_mgr mgr;
extern void http_handler(struct mg_connection *c, int ev, void *ev_data);

typedef struct {
   rrconn_t connection;
   server_cfg_t server;
} rrclient_transport_t;

static void rrclient_transport_handler(struct mg_connection *c, int ev, void *data) {
   rrconn_t *cptr = c->fn_data;
   if (!cptr) {
      return;
   }
   if (cptr != ws_conn) {
      if (ev == MG_EV_CLOSE) {
         free(cptr);
      }
      return;
   }
   if (ev == MG_EV_OPEN) {
      cptr->conn = c;
   }
   if (cptr->is_ws) {
      http_handler(c, ev, data);
      // Fatal WS error listeners release the native state before CLOSE.
      if (ev == MG_EV_ERROR && ws_conn != cptr) {
         c->fn_data = NULL;
      }
   } else {
      irc_mongoose_handler(c, ev, data);
   }
}

#endif // USE_MONGOOSE

static const unsigned int reconnect_delays[] = {
   1, 2, 5, 10, 30, 60
};
#define RRC_MAX_RECONNECTS 10

static bool reconnect_enabled = false;
static bool reconnect_pending = false;
static bool reconnect_attempting = false;
static unsigned int reconnect_tries = 0;
static time_t reconnect_at = 0;

static void rrclient_cancel_reconnect(void) {
   reconnect_enabled = false;
   reconnect_pending = false;
   reconnect_tries = 0;
   reconnect_at = 0;
}

static void rrclient_schedule_reconnect(void) {
   if (!reconnect_enabled || reconnect_pending || dying || !server_name || !*server_name) {
      return;
   }

   // Never schedule a reconnect while we're already trying to connect
   if (ws_connected) {
      return;
   }

   if (reconnect_tries >= RRC_MAX_RECONNECTS) {
      ui_print(NULL, "%s \00304Giving up after %u reconnect attempts\017", get_chat_ts(now), reconnect_tries);
      reconnect_enabled = false;

      return;
   }

   unsigned int delay_index = reconnect_tries;

   if (delay_index >= sizeof(reconnect_delays) / sizeof(reconnect_delays[0]) ) {
      delay_index = sizeof(reconnect_delays) / sizeof(reconnect_delays[0]) - 1;
   }
   unsigned int delay = reconnect_delays[delay_index];

   reconnect_tries++;
   reconnect_pending = true;
   reconnect_at = time(NULL) + delay;
   ws_connected = -1;
   tui_refresh_sb_online();
   ui_print(NULL, "%s \00308Reconnecting in %u second%s (attempt %u/%u)\017", get_chat_ts(now), delay, delay == 1 ? "" : "s", reconnect_tries,
      RRC_MAX_RECONNECTS);
}

static void rrclient_handle_auth_error_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   // Auth failures (bad login/password, kicked, disabled account) are not
   // transient: never auto-reconnect, the user must fix credentials and
   // connect manually. The red error display happens in the auth.error
   // handler in events.c
   rrclient_cancel_reconnect();
}

static void rrclient_handle_reconnect_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (strcasecmp(event, "authorized") == 0 || strcasecmp(event, "irc.connected") == 0) {
      reconnect_pending = false;
      reconnect_tries = 0;
      reconnect_at = 0;
   } else if (strcasecmp(event, "disconnected") == 0 || strcasecmp(event, "http.error") == 0) {
      // "disconnected" and "http.error" (MG_EV_ERROR) are connection failures:
      // schedule a reconnect. Plain "error" events are non-fatal protocol error
      // messages from the server (cli.error.c) and must NOT drop the connection.
      rrclient_schedule_reconnect();
   }
}

void connman_register_events(void) {
   event_on("authorized", rrclient_handle_reconnect_event, NULL);
   event_on("irc.connected", rrclient_handle_reconnect_event, NULL);
   event_on("disconnected", rrclient_handle_reconnect_event, NULL);
   event_on("http.error", rrclient_handle_reconnect_event, NULL);
   event_on("auth.error", rrclient_handle_auth_error_event, NULL);
   // NB: no event_on("error", ...) here - server protocol errors are non-fatal
}

// Reconnect engine poll - split from the mg_mgr_poll() side so the GTK GSource
// can block inside mg_mgr_poll() and still drive reconnects.
void rrclient_poll_events_reconnect(void) {
   if (reconnect_pending && time(NULL) >= reconnect_at) {
      reconnect_pending = false;

      // ws_connected == 1 means connected; 0 offline; -1 "trying" from
      // schedule_reconnect() - all but 1 are valid states to (re)connect in.
      if (reconnect_enabled && ws_connected != 1 && !dying) {
         reconnect_attempting = true;
         connect_server(server_name);
         reconnect_attempting = false;
      }
   }
}

///////////////////////////////////////////////////////////
// Handle properly connect, disconnect, and error events //
///////////////////////////////////////////////////////////
bool disconnect_server(const char *server) {
   Log(LOG_DEBUG, "connman", "disconnect_server: |%s|", server);

   rrclient_cancel_reconnect();
   rrclient_update_connection_ui(0);

   if (ws_conn) {
#ifdef  USE_MONGOOSE

      if (ws_conn->conn) {
         ws_conn->conn->is_closing = 1;
      }
#endif // defined(USE_MONGOOSE)
      ws_connected = false;
      userlist_clear_all();
   }

   return false;
}

// XXX: pass pointer to the server structure
bool connect_server(const char *server) {
   const char *resolved_server = rrclient_resolve_server_name(server);
   if (!resolved_server) {
      return true;
   }
   const char *url = get_server_property(resolved_server, "server.url");
   return rrclient_connect_url(resolved_server, url);
}

bool rrclient_connect_url(const char *profile, const char *url) {
   rr_server_url_t endpoint;
   if (!rr_server_url_parse(url, &endpoint)) {
      ui_print(NULL, "\00304Invalid server.url: use ws://host:port/path, wss://host:port/path, irc://host:port or ircs://host:port\017");
      return true;
   }
#ifdef USE_MONGOOSE
   if (ws_conn && ws_conn->conn && !ws_conn->conn->is_closing) {
      ui_print(NULL, "\00308Disconnect the current server before connecting\017");
      return true;
   }
   rrclient_transport_t *transport = calloc(1, sizeof(*transport));
   if (!transport) {
      return true;
   }
   char *name = strdup(profile && *profile ? profile : "direct");
   if (!name) {
      free(transport);
      return true;
   }
   free((void *)server_name);
   server_name = name;
   if (!reconnect_attempting) {
      reconnect_enabled = true;
      reconnect_pending = false;
      reconnect_tries = 0;
      reconnect_at = 0;
   }
   rrconn_t *cptr = &transport->connection;
   cptr->fd = -1;
   cptr->is_ws = !endpoint.irc;
   if (endpoint.irc) {
      cptr->server = &transport->server;
      snprintf(cptr->server->host, sizeof(cptr->server->host), "%s", endpoint.host);
      snprintf(cptr->server->network, sizeof(cptr->server->network), "%s", server_name);
      cptr->server->port = endpoint.port;
      cptr->server->tls = endpoint.tls;
      const char *nick = get_server_property(server_name, "server.user");
      const char *pass = get_server_property(server_name, "server.pass");
      const char *autojoin = get_server_property(server_name, "autojoin");
      if (!nick || !*nick) {
         nick = cfg_get("irc.nick");
         if (!nick || !*nick) {
            nick = "nonick";
         }
      }
      if (strlen(nick) >= sizeof(cptr->nick) ||
          (pass && strlen(pass) >= sizeof(cptr->server->pass)) ||
          (autojoin && strlen(autojoin) >= sizeof(cptr->server->autojoin))) {
         ui_print(NULL, "\00304IRC nickname, password or autojoin exceeds the protocol limit\017");
         free(transport);
         return true;
      }
      snprintf(cptr->nick, sizeof(cptr->nick), "%s", nick);
      snprintf(cptr->server->nick, sizeof(cptr->server->nick), "%s", cptr->nick);
      snprintf(cptr->server->pass, sizeof(cptr->server->pass), "%s", pass ? pass : "");
      snprintf(cptr->server->autojoin, sizeof(cptr->server->autojoin), "%s", autojoin ? autojoin : "");
      if (irc_init()) {
         free(transport);
         return true;
      }
   }
   ws_conn = cptr;
   ws_connected = -1;
   rrclient_update_connection_ui(-1);
   ui_print(NULL, "%s Connecting to %s", get_chat_ts(now), url);
   char connect_url[2048];
   const char *scheme = endpoint.irc ? (endpoint.tls ? "tls" : "tcp") :
      (endpoint.tls ? "wss" : "ws");
   int length = snprintf(connect_url, sizeof(connect_url), "%s://%s%s%s:%u%s", scheme,
      endpoint.ipv6 ? "[" : "", endpoint.host, endpoint.ipv6 ? "]" : "",
      endpoint.port, endpoint.irc ? "" : endpoint.path);
   if (length < 0 || (size_t)length >= sizeof(connect_url)) {
      free(ws_conn);
      ws_conn = NULL;
      ws_connected = 0;
      rrclient_update_connection_ui(0);
      return true;
   }
   struct mg_connection *connection = endpoint.irc ?
      mg_connect(&mgr, connect_url, rrclient_transport_handler, cptr) :
      mg_ws_connect(&mgr, connect_url, rrclient_transport_handler, cptr, NULL);
   if (!connection) {
      if (ws_conn == cptr) {
         free(ws_conn);
         ws_conn = NULL;
      }
      ws_connected = 0;
      event_emit("http.error", NULL, NULL);
      return true;
   }
   if (ws_conn == cptr) {
      cptr->conn = connection;
   }
   return false;
#else
   ui_print(NULL, "Server connections require the Mongoose transport backend");
   return true;
#endif
}

bool connect_or_disconnect(const char *server) {
   const char *resolved_server = rrclient_resolve_server_name(server);

   if (!resolved_server) {
      Log(LOG_WARN, "connman", "connect_or_disconnect called with no server");

      return true;
   }

   if (ws_connected) {
      disconnect_server(resolved_server);
   } else {
      if (!server_name || strcmp(server_name, resolved_server) != 0) {
         free( (void *)server_name);
         server_name = strdup(resolved_server);
      }
      connect_server(resolved_server);
   }

   return false;
}

void connman_autoconnect(void) {
   // Should we connect to a server on startup?
   const char *autoconnect = cfg_get_exp("server.auto-connect");

   if (autoconnect && *autoconnect) {
      char *tv = strdup(autoconnect);

      if (!tv) {
         abort();
      }
      // Split this on ',' and connect to allow configured servers
      char *sp = strtok(tv, ",");
      while (sp) {
         char this_server[256];
         memset(this_server, 0, sizeof(this_server) );
         snprintf(this_server, sizeof(this_server), "%s", sp);
         ui_print(NULL, "%s * Autoconnect profile: %s *", get_chat_ts(now), this_server);
         sp = strtok(NULL, ",");
         connect_or_disconnect(this_server);
      }
      free(tv);
      free( (void *)autoconnect);
      autoconnect = NULL;
   } else {
      show_server_chooser();
   }
}
