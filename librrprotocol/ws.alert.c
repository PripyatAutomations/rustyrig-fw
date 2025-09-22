//
// rrclient/ws.alert.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#define	__RRCLIENT	1
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
//#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
//#include "mod.ui.gtk3/gtk.core.h"
#include <librrprotocol/rrprotocol.h>

extern dict *cfg;		// config.c
extern time_t now;

bool ws_handle_alert_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg) {
      Log(LOG_WARN, "http.ws", "alert_msg: got msg:<%x> mg_conn:<%x>", msg, c);
      return true;
   }

   bool rv = false;

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   if (!msg->data.buf) {
      Log(LOG_WARN, "http.ws", "alert_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;

   // Copy to a null terminated buffer
   char buf[HTTP_WS_MAX_MSG+1];
   memset(buf, 0, sizeof(buf));
   memcpy(buf, msg_data.buf, msg_data.len);

   // and expand into a dict, which is freed in cleanup below
   dict *d = json2dict(buf);
   char *alert_msg = dict_get(d, "alert.msg", NULL);
   char *alert_from = dict_get(d, "alert.from", NULL);
   time_t alert_ts = dict_get_time_t(d, "alert.ts", 0);

   if (!alert_from) {
      alert_from = strdup("***SERVER***");
   }


   if (!alert_ts) {
      alert_ts = now;
   }

   if (alert_msg) {
// XXX: readd this
//      ui_print("[%s] ALERT: %s: %s !!!", get_chat_ts(alert_ts), alert_from, alert_msg);
   }

   dict_free(d);

   return false;
}
