//
// gtk-client/ws.notice.c
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
extern time_t now;

bool ws_handle_notice_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   bool rv = false;

   if (!c || !msg) {
      Log(LOG_WARN, "http.ws", "notice_msg: got msg:<%x> mg_conn:<%x>", msg, c);
      return true;
   }

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   if (!msg->data.buf) {
      Log(LOG_WARN, "http.ws", "notice_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *notice_msg = mg_json_get_str(msg_data, "$.notice.msg");
   char *notice_from = mg_json_get_str(msg_data, "$.notice.from");

   if (!notice_from) {
      notice_from = strdup("***SERVER***");
   }

   if (notice_msg) {
      ui_print("[%s] NOTICE: %s: %s !!!", get_chat_ts(), notice_from, notice_msg);
   }
   free(notice_msg);
   free(notice_from);

   return false;
}
