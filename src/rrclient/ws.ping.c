//
// gtk-client/ws.ping.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
extern bool cfg_show_pings;
extern time_t now;

bool ws_handle_ping_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg) {
      Log(LOG_WARN, "http.ws", "ping_msg: got msg:<%x> mg_conn:<%x>", msg, c);
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
      Log(LOG_WARN, "http.ws", "ping_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;

   char ts_buf[32];
   double ping_ts = 0;
   mg_json_get_num(msg_data, "$.ping.ts", &ping_ts);

   if (ping_ts > 0) {
      memset(ts_buf, 0, sizeof(ts_buf));
      snprintf(ts_buf, sizeof(ts_buf), "%.0f", ping_ts);

      char pong[128];
      snprintf(pong, sizeof(pong), "{ \"pong\": { \"ts\":\"%s\" } }", ts_buf);
      mg_ws_send(c, pong, strlen(pong), WEBSOCKET_OP_TEXT);
   }

   if (cfg_show_pings) {
      ui_print("[%s] * Ping? Pong! *", get_chat_ts());
   }

   return false;
}
