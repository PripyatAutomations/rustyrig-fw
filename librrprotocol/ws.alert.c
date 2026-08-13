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

extern dict *cfg;                // config.c
extern time_t now;

#if     defined(USE_MONGOOSE)
bool ws_handle_alert_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg) {
      Log(LOG_WARN, "http.ws", "alert_msg: got msg:<%p> mg_conn:<%p>", msg, c);

      return true;
   }
   bool rv = false;

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;

   if (c->rem.is_ip6) {
      inet_ntop( AF_INET6, c->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &c->rem.addr.ip4, ip, sizeof(ip) );
   }

   if (!msg->data.buf) {
      Log(LOG_WARN, "http.ws", "alert_msg: got msg from msg_conn:<%p> from %s:%d -- msg:<%p> with no data ptr", c, ip,
         port, msg);

      return true;
   }
   struct mg_str msg_data = msg->data;

   // Copy to a null terminated buffer
   char buf[HTTP_WS_MAX_MSG + 1];
   memset( buf, 0, sizeof(buf) );
   memcpy(buf, msg_data.buf, msg_data.len);

   // and expand into a dict, which is freed in cleanup below
   dict *d = json2dict(buf);
   char *alert_msg = dict_get(d, "alert.msg", NULL);
   char *alert_from = dict_get(d, "alert.from", NULL);
   time_t alert_ts = dict_get_time_t(d, "alert.ts", 0);

   if (!alert_from) {
      dict_add(d, "alert.from", "***SERVER***");
   }

   if (!alert_ts) {
      char now_ts[12];
      memset(now_ts, 0, 12);
      snprintf(now_ts, 12, "%lu", now);
      dict_add(d, "alert.ts", now_ts);
   }

   if (alert_msg) {
      const char *jp = dict2json(d);
      event_emit("alert", NULL, jp);
      free( (void *)jp );
   }
   dict_free(d);

   return false;
}

#endif // defined(USE_MONGOOSE)
