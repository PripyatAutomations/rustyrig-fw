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
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/config.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/connman.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"
#include "rrclient/ui.h"
#include "rrclient/gtk.core.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern rr_connection_t *active_connections;
extern struct mg_mgr mgr;
extern dict *cfg;
extern dict *servers;
rr_connection_t *active_connections;

char active_server[512];
bool ws_connected = false;	// Is RX stream connecte?
bool ws_tx_connected = false;	// Is TX stream connected?
struct mg_connection *ws_conn = NULL, *ws_tx_conn = NULL;
bool server_ptt_state = false;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];

// ws.c
extern void http_handler(struct mg_connection *c, int ev, void *ev_data);


rr_connection_t *connection_find(const char *server) {
   if (!server) {
      return NULL;
   }

   rr_connection_t *cptr = NULL;

   while (cptr) {
      if (strcasecmp(cptr->name, server) == 0) {
         Log(LOG_CRAZY, "connman", "Found server |%s| at <%x>", server, cptr);
         return cptr;
      }
      cptr = cptr->next;
   }
   return NULL;
}

bool connection_create(const char *server) {
   if (!server) {
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
bool disconnect_server(void) {
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
bool connect_server(void) {
   // This could recurse in an ugly way if not careful...
   if (active_server[0] == '\0') {
      show_server_chooser();
      return true;
   }

   const char *url = get_server_property(active_server, "server.url");

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
      ui_print("[%s] * Server '%s' does not have a server.url configured! Check your config or maybe you mistyped it?", active_server);
   }

   return false;
}

#if	defined(USE_GTK)
bool connect_or_disconnect(GtkButton *button) {
   if (ws_connected) {
      disconnect_server();
   } else {
      connect_server();
   }
   return false;
}
#endif	// defined(USE_GTK)
