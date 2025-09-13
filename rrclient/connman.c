//
// src/rrclient/connman.c: Connection Manager
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// XXX: This needs finished to fully support multiple connections in one client
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/config.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/connman.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"
#include "rrclient/ui.h"
#include "rrclient/gtk.core.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "librustyaxe/client-flags.h"

// Server connections
rr_connection_t *active_connections;
bool ws_connected = false;	// Is RX stream connecte?
bool ws_tx_connected = false;	// Is TX stream connected?
struct mg_connection *ws_conn = NULL, *ws_tx_conn = NULL;
bool server_ptt_state = false;
// XXX: this needs to go away and be replaced with http_find_servername(c)
const char *server_name = NULL;

extern rr_connection_t *active_connections;
extern struct mg_mgr mgr;
extern dict *cfg;
extern dict *servers;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern void http_handler(struct mg_connection *c, int ev, void *ev_data);

rr_connection_t *connection_find(const char *server) {
   if (!server) {
      Log(LOG_DEBUG, "connman", "connection_find without server");
      return NULL;
   }

   rr_connection_t *cptr = NULL;

   while (cptr) {
      if (strcasecmp(cptr->name, server) == 0) {
         Log(LOG_CRAZY, "connman", "Found server |%s| at <%x>", server, cptr);
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

   // Look up the connection properties from the server blocks and return true if not found
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
      Log(LOG_CRIT, "ws", "get_server_prop with null server:<%x> or prop:<%x>");
      return NULL;
   }

   char *rv = NULL;

   char fullkey[1024];
   memset(fullkey, 0, sizeof(fullkey));
   snprintf(fullkey, sizeof(fullkey), "%s.%s", server, prop);
   rv = dict_get(servers, fullkey, NULL);
#if	defined(DEBUG_CONFIG) || defined(DEBUG_CONFIG_SERVER)
   ui_print("Looking up server key: %s returned %s", fullkey, (rv ? rv : "NULL"));
   dict_dump(servers, stdout);
#endif
   return rv;
}

///////////////////////////////////////////////////////////
// Handle properly connect, disconnect, and error events //
///////////////////////////////////////////////////////////
bool disconnect_server(const char *server) {
   Log(LOG_DEBUG, "connman", "disconnect_server: |%s|", server);

   if (ws_connected) {
      if (ws_conn) {
         ws_conn->is_closing = 1;
      }
      ws_connected = false;
      gtk_button_set_label(GTK_BUTTON(conn_button), "Connect");
      // XXX: im not sure this is acceptable
//      ws_conn = NULL;
      userlist_clear_all();
   }
   return false;
}

// XXX: pass pointer to the server structure
bool connect_server(const char *server) {
   if (!server) {
      Log(LOG_DEBUG, "connman", "connect_server with no server name!");

   }
   const char *url = get_server_property(server, "server.url");
   Log(LOG_DEBUG, "connman", "server: |%s| url: |%s|", server, url);

   if (url) {
// XXX:
#if	defined(USE_GTK)
      gtk_button_set_label(GTK_BUTTON(conn_button), "----------");
#endif	// defined(USE_GTK)
      ui_print("[%s] Connecting to %s", get_chat_ts(), url);

      ws_conn = mg_ws_connect(&mgr, url, http_handler, NULL, NULL);

      if (!ws_conn) {
         ui_print("[%s] Socket connect error", get_chat_ts());
      }
   } else {
      ui_print("[%s] * Server '%s' does not have a server.url configured! Check your config or maybe you mistyped it?", server);
   }

   return false;
}

#if	defined(USE_GTK)
bool connect_or_disconnect(const char *server, GtkButton *button) {
   if (ws_connected) {
      disconnect_server(server);
   } else {
      if (!server_name) {
         server_name = strdup(server);
      }
      connect_server(server);
   }
   return false;
}
#endif	// defined(USE_GTK)

void connman_autoconnect(void) {
   // Should we connect to a server on startup?
   const char *autoconnect = cfg_get_exp("server.auto-connect");

   if (autoconnect) {
      char *tv = strdup(autoconnect);
      // Split this on ',' and connect to allow configured servers
      char *sp = strtok(tv, ",");
      while (sp) {
         char this_server[256];
         memset(this_server, 0, sizeof(this_server));
         snprintf(this_server, sizeof(this_server), "%s", sp);
         ui_print("* Autoconnecing to profile: %s *", this_server);
         sp = strtok(NULL, ",");
         connect_or_disconnect(this_server, GTK_BUTTON(conn_button));
      }
      free(tv);
      free((void *)autoconnect);
      autoconnect = NULL;
   } else {
      show_server_chooser();
   }
}
