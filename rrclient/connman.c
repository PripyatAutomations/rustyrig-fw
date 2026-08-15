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
#include <ev.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/userlist.h>

// Server connections
rr_connection_t *active_connections;
bool ws_connected = false;       // Is RX stream connected?
bool ws_tx_connected = false;    // Is TX stream connected?
bool server_ptt_state = false;
const char *login_user = NULL;

extern rr_connection_t *active_connections;
extern dict *cfg;
extern struct ev_loop *loop;
extern bool dying;
extern bool debug_sockets;
extern time_t now, poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN + 1];

static const char *rrclient_resolve_server_name(const char *requested_server) {
   if (requested_server && *requested_server) {
      return requested_server;
   }
   const char *autoconnect = cfg_get_exp("server.auto-connect");

   if (autoconnect && *autoconnect) {
      return autoconnect;
   }

   if (server_name && *server_name) {
      return server_name;
   }

   return NULL;
}

#ifdef	USE_MONGOOSE
struct mg_connection *ws_conn = NULL, *ws_tx_conn = NULL;
extern struct mg_mgr mgr;
extern void http_handler(struct mg_connection *c, int ev, void *ev_data);
extern struct mg_connection *ws_conn;
#endif	// USE_MONGOOSE
extern bool ws_connected;

char session_token[HTTP_TOKEN_LEN + 1] = {
   0
};


#ifdef	USE_MONGOOSE
static void rrclient_ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;

      if (msg && msg->data.buf) {
         char buf[HTTP_WS_MAX_MSG + 1];
         memset( buf, 0, sizeof(buf) );
         memcpy(buf, msg->data.buf, msg->data.len);
         dict *d = json2dict(buf);

         if (!d) {
            return;
         }
         char *cmd = dict_get(d, "talk.cmd", NULL);
         char *pong_ts = dict_get(d, "pong.ts", NULL);
         char *ping_ts = dict_get(d, "ping.ts", NULL);

         // It's a ping, so we should reply to it with a pong then fall through
         // to cleanup
         if (ping_ts) {
            const char *jp = dict2json_mkstr( VAL_STR, "type", "pong", VAL_ULONG, "ts", atol(ping_ts) );
            mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (void *)jp );
         } else if (pong_ts) {
            Log(LOG_CRAZY, "pong", "Received pong ts:%s", pong_ts);
         } else if (cmd && strcasecmp(cmd, "msg") == 0) {
            char *from = dict_get(d, "talk.from", NULL);
            char *data = dict_get(d, "talk.data", NULL);
            char *msg_type = dict_get(d, "talk.msg_type", NULL);
            char *target = dict_get(d, "talk.target", NULL);
            time_t ts = dict_get_time_t(d, "talk.ts", now);

            if (from && data) {
               event_emit("talk.msg", NULL, buf);
            }
         } else if (dict_get(d, "hello", NULL) ) {
            Log(LOG_DEBUG, "ws", "Got hello from server");
         } else if (dict_get(d, "auth.cmd", NULL) ) {
            Log(LOG_DEBUG, "ws", "Got auth message");
         }
         dict_free(d);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      ws_connected = true;
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;

      char buf[HTTP_WS_MAX_MSG + 1] = {
         0
      };

      if (msg && msg->data.buf) {
         memset( buf, 0, sizeof(buf) );
         memcpy(buf, msg->data.buf, msg->data.len);
      }
      event_emit("connected", NULL, buf);
      ui_print(NULL, "status", "Connected to server");
      const char *jp = dict2json_mkstr(VAL_STR, "hello", "rrclient", VAL_STR, "hello.version", VERSION);
      mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
      free( (void *)jp );
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = false;
      event_emit("disconnected", NULL, NULL);
      ui_print(NULL, "status", "Disconnected from server");
   }
}
#endif	// USE_MONGOOSE

bool rrclient_connect(const char *url) {
   if (!url) {
      return true;
   }
   ui_print(NULL, "status", "Connecting to %s", url);
#ifdef	USE_MONGOOSE
   ws_conn = mg_ws_connect(&mgr, url, rrclient_ws_handler, NULL, NULL);

   if (!ws_conn) {
      ui_print(NULL, "status", "Connection failed");

      return true;
   }
#endif	// USE_MONGOOSE

   return false;
}

bool rrclient_send_chat(const char *data) {
   if (!data) {
      return true;
   }
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", data, VAL_STR, "talk.msg_type",
      "pub");

   if (!jp) {
      return true;
   }

#ifdef	USE_MONGOOSE
   if (!ws_conn) {
      return true;
   }
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif	// USE_MONGOOSE
   free( (void *)jp );

   return false;
}

bool rrclient_send(const char *json) {
   if (!json) {
      return true;
   }

#ifdef	USE_MONGOOSE
   if (!ws_conn) {
      return true;
   }
   mg_ws_send(ws_conn, json, strlen(json), WEBSOCKET_OP_TEXT);
#endif	// USE_MONGOOSE
   return false;
}

bool rrclient_disconnect(void) {
#ifdef	USE_MONGOOSE
   if (ws_conn) {
      ws_conn->is_closing = 1;
      ws_conn = NULL;
   }
#endif	// USE_MONGOOSE
   ws_connected = false;

   return false;
}

void rrclient_poll_events(void) {
#ifdef	USE_MONGOOSE
   mg_mgr_poll(&mgr, 0);
#endif	// USE_MONGOOSE
}

bool rrclient_autoconnect(void) {
   const char *server = cfg_get_exp("server.auto-connect");

   if (server) {
      char sname[256];
      snprintf(sname, sizeof(sname), "%s", server);
      free( (void *)server );

      char fullkey[1024];
      snprintf(fullkey, sizeof(fullkey), "server:%s.server.url", sname);
      const char *url = cfg_get_exp(fullkey);

      if (url) {
         rrclient_connect(url);
         free( (void *)url );
      }
   }

   return false;
}

rr_connection_t *connection_find(const char *server) {
   if (!server) {
      Log(LOG_DEBUG, "connman", "connection_find without server");

      return NULL;
   }
   rr_connection_t *cptr = NULL;

   while (cptr) {
      if (strcasecmp(cptr->name, server) == 0) {
         Log(LOG_CRAZY, "connman", "Found server |%s| at <%p>", server, cptr);
         server_name = strdup(server);

         return cptr;
      }
      cptr = cptr->next;
   }
   return NULL;
}

bool connection_create(const char *server) {
   if (!server) {
      Log(LOG_DEBUG, "connman", "connection_create without server");

      return true;
   }
   // Look up the connection properties from the server blocks and return true
   // if not found
   rr_connection_t *cptr = connection_find(server);

   if (cptr) {
      // Server already exists
      Log(LOG_WARN, "connman", "Connection for server |%s| already exists", server);

      return true;
   }

   // Connect to the server

   // Add to the connection list
   return false;
}

bool connection_remove(rr_connection_t *conn) {
   if (!conn) {
      Log(LOG_DEBUG, "connman", "connection_remove called with no connection ptr");

      return true;
   }

   return false;
}

const char *get_server_property(const char *server, const char *prop) {
   if (!server || !prop) {
      Log(LOG_CRIT, "ws", "get_server_prop with null server:<%p> or prop:<%p>");

      return NULL;
   }
   char fullkey[1024];
   memset( fullkey, 0, sizeof(fullkey) );
   snprintf(fullkey, sizeof(fullkey), "server:%s.%s", server, prop);

   return dict_get(cfg, fullkey, NULL);
}

///////////////////////////////////////////////////////////
// Handle properly connect, disconnect, and error events //
///////////////////////////////////////////////////////////
bool disconnect_server(const char *server) {
   Log(LOG_DEBUG, "connman", "disconnect_server: |%s|", server);

   if (ui_mode == UI_MODE_GTK) {
#ifdef  USE_GTK
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_button_set_label(GTK_BUTTON(conn_button), "Offline");
      gtk_style_context_add_class(ctx, "conn-idle");
      gtk_style_context_remove_class(ctx, "conn-pending");
      gtk_style_context_remove_class(ctx, "conn-active");
#endif // defined(USE_GTK)
   }

   if (ws_connected) {
#if     defined(USE_MONGOOSE)
      if (ws_conn) {
         ws_conn->is_closing = 1;
      }
#endif // defined(USE_MONGOOSE)
      ws_connected = false;

#ifdef  USE_GTK
      gtk_button_set_label(GTK_BUTTON(conn_button), "Offline");
#endif // defined(USE_GTK)
      userlist_clear_all();
   }

   return false;
}

// XXX: pass pointer to the server structure
bool connect_server(const char *server) {
   const char *resolved_server = rrclient_resolve_server_name(server);

   if (!resolved_server) {
      Log(LOG_DEBUG, "connman", "connect_server with no server name!");

      return true;
   }
   const char *url = get_server_property(resolved_server, "server.url");
   Log(LOG_DEBUG, "connman", "server: |%s| url: |%s|", resolved_server, url);

   if (url) {

      if (ui_mode == UI_MODE_GTK) {
#ifdef  USE_GTK
         GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
         gtk_button_set_label(GTK_BUTTON(conn_button), "Offline");
         gtk_style_context_add_class(ctx, "conn-pending");
         gtk_style_context_remove_class(ctx, "conn-active");
         gtk_style_context_remove_class(ctx, "conn-idle");
         gtk_button_set_label(GTK_BUTTON(conn_button), "trying..");
#endif // defined(USE_GTK)
      } else if (ui_mode == UI_MODE_TUI) {
      }
      ui_print(NULL, "%s Connecting to %s", get_chat_ts(now), url);

#if     defined(USE_MONGOOSE)
      ws_conn = mg_ws_connect(&mgr, url, http_handler, NULL, NULL);

      if (!ws_conn) {
         ui_print( NULL, "%s Socket connect error", get_chat_ts(now) );
      }
#endif // defined(USE_MONGOOSE)
   } else {
      ui_print(NULL,
         "[%s] * Server '%s' does not have a server.url configured! Check your config or maybe you mistyped it?",
         resolved_server);
   }

   return false;
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
         free( (void *)server_name );
         server_name = strdup(resolved_server);
      }
      connect_server(resolved_server);
   }

   return false;
}

void connman_autoconnect(void) {
   // Should we connect to a server on startup?
   const char *autoconnect = cfg_get_exp("server.auto-connect");

   if (autoconnect) {
      char *tv = strdup(autoconnect);

      if (!tv) {
         abort();
      }
      // Split this on ',' and connect to allow configured servers
      char *sp = strtok(tv, ",");
      while (sp) {
         char this_server[256];
         memset( this_server, 0, sizeof(this_server) );
         snprintf(this_server, sizeof(this_server), "%s", sp);
         ui_print(NULL, "%s * Autoconnect profile: %s *", get_chat_ts(now), this_server);
         sp = strtok(NULL, ",");
         connect_or_disconnect( this_server );
      }
      free(tv);
      free( (void *)autoconnect );
      autoconnect = NULL;
   } else {
      show_server_chooser();
   }
}
