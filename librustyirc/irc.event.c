#include "internal.h"
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only

void rustyirc_emit_message(const char *event, irc_conn_t *cptr, const irc_message_t *mp) {
   if (!event || !mp || mp->argc < 1 || !mp->argv) {
      return;
   }
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "msg.cmd", mp->argv[0]);
   dict_add(d, "msg.prefix", mp->prefix ? mp->prefix : "");
   dict_add_int(d, "msg.argc", mp->argc);

   for (int i = 0 ; i < mp->argc ; i++) {
      char key[32];
      snprintf(key, sizeof(key), "msg.arg%d", i);
      dict_add(d, key, mp->argv[i]);
   }

   rustyirc_event_emit_dict(event, cptr, d);
   dict_free(d);
}
