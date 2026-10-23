#include "internal.h"
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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
