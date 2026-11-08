//
// rrclient/ws.alert.c: Handle alerts in client side
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

extern dict *cfg;               // config.c
extern time_t now;

bool ws_handle_alert_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "alert_msg: got cptr:<%p> d:<%p>", cptr, d);

      return false;
   }
   bool rv = true;
   char *ip = cptr->user_ip;
   int port = cptr->user_port;

   const char *alert_msg = dict_get(d, "alert.msg", NULL);

   if (!alert_msg) {
      return false;
   }

   const char *alert_from = dict_get(d, "alert.from", NULL);
   time_t alert_ts = dict_get_time_t(d, "alert.ts", 0);

   if (!alert_from) {
      dict_add(d, "alert.from", "***SERVER***");
   }

   if (!alert_ts) {
      dict_add_ulong(d, "alert.ts", now);
   }

   event_emit_dict("alert", cptr, d);

   return true;
}
