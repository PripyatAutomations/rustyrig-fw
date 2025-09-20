//
// rrclient/ws.ping.c
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
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include <librustyaxe/client-flags.h>

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

   time_t ping_ts = dict_get_time_t(d, "ping.ts", 0);

   if (ping_ts) {
      const char *jp = dict2json_mkstr(VAL_ULONG, "pong.ts", ping_ts);
      mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
      free((char *)jp);
   } else {
      ui_print("*** Empty ping?? ***");
   }

   if (cfg_show_pings) {
      ui_print("[%s] * Ping? Pong! *", get_chat_ts(ping_ts));
   }

   return false;
}
