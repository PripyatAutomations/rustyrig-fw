//
// rrgtk/srv.ping.c: Server side ping handling
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern const char *get_server_property(const char *server, const char *prop);
extern bool cfg_http_debug;
extern time_t now;
extern dict *cfg;               // config.c
bool cfg_show_pings = true;     // cfg:ui.show-pings=false in rrserver.cfg

// Global last measured ping RTT in ms, updated by ws_handle_pong() in srv.http.c
long long last_ping_rtt_ms = -1;

bool ws_send_ping(rrconn_t *cptr) {
   if (!cptr || !cptr->is_ws) {
      return false;
   }
   char resp_buf[HTTP_WS_MAX_MSG + 1];

   if (!cptr) {
      Log(LOG_DEBUG, "auth", "ws_send_ping for null cptr!");

      return false;
   }
#ifdef  USE_MONGOOSE

   if (!cptr->conn && !cptr->native_transport) {
      Log(LOG_DEBUG, "auth", "ws_send_ping for cptr:<%p> has mg_conn:<%p> and is invalid", cptr, (cptr ? cptr->conn : NULL));

      return false;
   }
#endif // USE_MONGOOSE

   // Make sure that timeout will happen if no response
   cptr->last_ping = now;
   cptr->ping_attempts++;

   // only bother making noise if the first attempt failed, send the first ping
   // to crazy level log
   if (cptr->ping_attempts > 1) {
      Log(LOG_DEBUG, "ping", "sending ping to user %s on cptr:<%p> with ts:[%li] attempt %d", cptr->chatname, cptr, now, cptr->ping_attempts);
   } else {
      Log(LOG_CRAZY, "ping", "sending ping to user %s on cptr:<%p> with ts:[%li] attempt %d", cptr->chatname, cptr, now, cptr->ping_attempts);
   }
   dict *d = dict_new();

   if (!d) {
      return false;
   }
   dict_add(d, "msg.type", "ping");
   dict_add_ulong(d, "msg.ts", now);
   // Monotonic microsecond timestamp for RTT measurement; echoed back in the pong
   dict_add_llong(d, "ping.ts", mono_us());
   bool sent = ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return sent;
}
