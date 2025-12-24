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
//#include "../ext/libmongoose/mongoose.h"
//#include "rrclient/ws.h"

//extern dict *cfg;		// config.c
//extern bool cfg_show_pings;

#if	defined(USE_MONGOOSE)
bool ws_handle_ping_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      Log(LOG_WARN, "http.ws", "ping_msg: got d:<%p> mg_conn:<%p>", d, c);
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
      Log(LOG_WARN, "ws.ping", "*** Empty ping?? ***");
   }

//   if (cfg_show_pings) {
//      Log(LOG_DENUG, "* Ping? Pong! %lu *", ping_ts);
//   }

   return false;
}

#endif	// defined(USE_MONGOOSE)
