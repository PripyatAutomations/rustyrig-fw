//
// rrclient/ws.ping.c
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
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/media.health.h>

extern dict *cfg;               // config.c
extern bool cfg_show_pings;

bool ws_handle_ping_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "ping_msg: got d:<%p> cptr:<%p>", d, cptr);

      return false;
   }
   bool rv = true;

   char *ip = cptr->user_ip;
   int port = cptr->user_port;
   time_t ping_ts = dict_get_time_t(d, "msg.ts", 0);

   if (ping_ts) {
      dict *pong_msg = dict_new();
      dict_add(pong_msg, "msg.type", "pong");
      dict_add_ulong(pong_msg, "msg.ts", ping_ts);
      // Echo the server's monotonic ping.ts (real microseconds) back so it can measure
      // RTT
      long long mono_ts = dict_get_llong(d, "ping.ts", 0);

      if (mono_ts) {
         dict_add_llong(pong_msg, "ping.ts", mono_ts);
      }
      ws_send_dict(NULL, cptr, pong_msg, WEBSOCKET_OP_TEXT);
      dict_free(pong_msg);
   } else {
      Log(LOG_WARN, "ws.ping", "*** Empty ping?? ***");
   }

   if (cfg_show_pings) {
      Log(LOG_CRAZY, "ws.ping", "* Ping? Pong! %lld *", ping_ts);
   }

   return true;
}

// Handle a PONG reply from the server: log RTT like webui.latencycalc.js does
bool ws_handle_pong_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "pong_msg: got d:<%p> cptr:<%p>", d, cptr);

      return false;
   }

   time_t pong_ts = dict_get_time_t(d, "msg.ts", 0);

   if (!pong_ts) {
      Log(LOG_WARN, "ws.pong", "PONG with no timestamp from server");

      return false;
   }

   time_t now = time(NULL);
   Log(LOG_CRAZY, "ws.pong", "* Pong! RTT: %lld secs *", (long long)(now - pong_ts));

   // Echoed monotonic ping.ts from the server's PING: diff against our own clock for ms
   // RTT
   // PARITY: librrprotocol/srv.http.c ws_handle_pong()
   long long ping_mono = dict_get_llong(d, "ping.ts", 0);

   if (ping_mono) {
      long long rtt_ms = (mono_us() - ping_mono) / 1000;

      if (rtt_ms < 0) {
         rtt_ms = 0;
      }
      last_ping_rtt_ms = rtt_ms;
      cptr->ping_rtt_ms = rtt_ms;
      rr_media_rtt_sample(cptr, (uint64_t) rtt_ms * 1000);
      Log(LOG_CRAZY, "ws.pong", "Client-side RTT: %lldms", rtt_ms);
   }

   return true;
}
