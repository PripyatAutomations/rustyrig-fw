//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Release PTT when a protocol session disconnects, independently of socket backend. */
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/http.h>

void ws_release_ptt_on_disconnect(rrconn_t *cptr) {
   if (!cptr || !cptr->is_ptt) {
      return;
   }

   dict *rig_msg = dict_new();

   if (!rig_msg) {
      Log(LOG_CRIT, "http", "Unable to allocate disconnect PTT release for %s", cptr->chatname[0] ? cptr->chatname : "(unknown)");

      return;
   }
   dict_add(rig_msg, "msg.type", "cat");
   dict_add(rig_msg, "cat.cmd", "ptt");
   dict_add_bool(rig_msg, "cat.ptt", false);
   dict_add(rig_msg, "cat.user", cptr->chatname);

   if (cptr->ptt_room[0]) {
      dict_add(rig_msg, "cat.room", cptr->ptt_room);
   }

   if (cptr->ptt_vfo) {
      char vfo_buf[2] = {
         cptr->ptt_vfo, '\0'
      };
      dict_add(rig_msg, "cat.vfo", vfo_buf);
   }
   cptr->is_ptt = false;
   event_emit_dict("rig.ptt", cptr, rig_msg);
   dict_free(rig_msg);
}
