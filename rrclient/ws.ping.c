//
// rrclient/ws.ping.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "librustyaxe/config.h"
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
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/util.file.h"
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "librustyaxe/client-flags.h"

extern dict *cfg;		// config.c
extern bool cfg_show_pings;
extern time_t now;

bool ws_handle_ping_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      Log(LOG_WARN, "http.ws", "ping_msg: got d:<%x> mg_conn:<%x>", d, c);
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

   char ts_buf[32];
   char *ping_ts_s = dict_get(d, "ping.ts", NULL);
   double ping_ts = 0;
   if (ping_ts_s) {
      ping_ts = atof(ping_ts_s);
   }

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
