//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
#include <rrclient/connman.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/vfo.h>

extern char session_token[HTTP_TOKEN_LEN + 1];
const char *login_user = NULL;

#ifdef  USE_MONGOOSE
extern struct mg_mgr mgr;
rrconn_t *ws_conn = NULL;
rrconn_t *ws_tx_conn = NULL;

#endif // USE_MONGOOSE

bool rrclient_connect(const char *url) {
   return rrclient_connect_url(server_name, url);
}

bool rrclient_disconnect(void) {
   return disconnect_server(server_name);
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
      free( (void *)server);

      char fullkey[1024];
      snprintf(fullkey, sizeof(fullkey), "server:%s.server.url", server_name);
      const char *url = cfg_get_exp(fullkey);

      if (url) {
         rrclient_connect(url);
         free( (void *)url);
      }
   }

   return false;
}
