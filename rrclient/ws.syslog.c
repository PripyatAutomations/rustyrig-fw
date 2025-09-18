//
// rrclient/ws.syslog.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/config.h>
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
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/posix.h>
#include <librustyaxe/util.file.h>
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"
#include <librustyaxe/client-flags.h>

extern dict *cfg;		// config.c
extern time_t now;

bool ws_handle_syslog_msg(struct mg_connection *c, dict *d) {
   bool rv = false;

   if (!c || !d) {
      Log(LOG_WARN, "http.ws", "syslog_msg: got d:<%x> mg_conn:<%x>", d, c);
      return true;
   }

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   char *ts = dict_get(d, "syslog.ts", NULL);
   char *prio = dict_get(d, "syslog.prio", NULL);
   char *subsys = dict_get(d, "syslog.subsys", NULL);
   char *data = dict_get(d, "syslog.data", NULL);
   char my_timestamp[64];
   time_t t;
   struct tm *tmp;
   memset(my_timestamp, 0, sizeof(my_timestamp));
   t = time(NULL);

   if ((tmp = localtime(&t))) {
      // success, proceed
      if (strftime(my_timestamp, sizeof(my_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         // handle the error 
         memset(my_timestamp, 0, sizeof(my_timestamp));
         snprintf(my_timestamp, sizeof(my_timestamp), "<%ld>", (long)time(NULL));
      }
   }

   logpriority_t log_priority = log_priority_from_str(prio);

   Log(LOG_DEBUG, "server.syslog", "Got message <%s.%s> %s", subsys, prio, data);
   log_print(log_priority, subsys, "[%s] <%s.%s> %s", my_timestamp, subsys, prio, data);
   return false;
}
