//
// rrclient/ws.notice.c
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
#include "librustyaxe/json.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/util.file.h"
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "librustyaxe/client-flags.h"

extern dict *cfg;		// config.c
extern time_t now;

bool ws_handle_notice_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg) {
      Log(LOG_WARN, "http.ws", "notice_msg: got msg:<%x> mg_conn:<%x>", msg, c);
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
      Log(LOG_WARN, "http.ws", "notice_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;

   char buf[HTTP_WS_MAX_MSG+1];
   memset(buf, 0, sizeof(buf));
   memcpy(buf, msg_data.buf, msg_data.len);

   // and expand into a dict, which is freed in cleanup below
   dict *d = json2dict(buf);

   char *notice_msg = dict_get(d, "notice.msg", NULL);
   char *notice_from = dict_get(d, "notice.from", NULL);
   // Copy to a null terminated buffer
   if (!notice_from) {
      notice_from = strdup("***SERVER***");
   }

   if (notice_msg) {
      ui_print("[%s] NOTICE: %s: %s !!!", get_chat_ts(), notice_from, notice_msg);
   }

   dict_free(d);

   return false;
}
