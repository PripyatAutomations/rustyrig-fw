// rrclient/rrclient.c
// Client connection state & core connect/disconnect/poll/autoconnect.
// Moved here from librrprotocol/rrclient.c - the library must not contain
// client behavior.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/rrclient.h>
#include <rrclient/ui.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/vfo.h>

extern char session_token[HTTP_TOKEN_LEN + 1];
const char *login_user = NULL;

#ifdef  USE_MONGOOSE
extern struct mg_mgr mgr;
rrconn_t *ws_conn = NULL;
rrconn_t *ws_tx_conn = NULL;

static void rrclient_ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   Log(LOG_CRIT, "rrclient", "rrclient_ws_handler() called");
}
#endif // USE_MONGOOSE

bool rrclient_connect(const char *url) {
   if (!url) {
      return true;
   }
   event_emit("connecting", NULL, NULL);

#ifdef  USE_MONGOOSE
   if (!ws_conn) {
      event_emit("http.error", NULL, NULL);
      return true;
   }

   ws_conn->conn = mg_ws_connect(&mgr, url, rrclient_ws_handler, NULL, NULL);
#endif // USE_MONGOOSE

   return false;
}

bool rrclient_disconnect(void) {
#ifdef USE_MONGOOSE
   if (ws_conn) {
      mg_ws_send(ws_conn->conn, NULL, 0, WEBSOCKET_OP_CLOSE);
      ws_conn->conn->is_closing = 1;
      ws_conn = NULL;
   }
#endif // USE_MONGOOSE
   ws_connected = false;

   return false;
}

void rrclient_poll_events(void) {
#ifdef  USE_MONGOOSE
   mg_mgr_poll(&mgr, 0);
#endif // USE_MONGOOSE
}

bool rrclient_autoconnect(void) {
   const char *server = cfg_get_exp("server.auto-connect");

   if (server) {
      char server_name[256];
      snprintf(server_name, sizeof(server_name), "%s", server);
      free( (void *)server );

      char fullkey[1024];
      snprintf(fullkey, sizeof(fullkey), "server:%s.server.url", server_name);
      const char *url = cfg_get_exp(fullkey);

      if (url) {
         rrclient_connect(url);
         free( (void *)url );
      }
   }

   return false;
}
