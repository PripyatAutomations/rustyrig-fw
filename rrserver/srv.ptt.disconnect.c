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
#include <rrserver/ptt.h>

void ws_release_ptt_on_disconnect(rrconn_t *cptr) {
   if (!cptr || !cptr->is_ptt) {
      return;
   }

   dict *rig_msg = dict_new();

   if (!rig_msg) {
      /* Do not strand a keyed transmitter merely because message allocation
       * failed. This fallback uses the normal safety release interlock and
       * remains best-effort if the hardware worker itself is unavailable. */
      Log(LOG_CRIT, "ptt", "Cannot allocate disconnect release for %s; forcing emergency PTT-off", cptr->chatname[0] ? cptr->chatname : "(unknown)");
      if (rr_ptt_set_all_off_reason("disconnect-oom")) {
         rr_ptt_set_blocked(true);
         Log(LOG_CRIT, "ptt", "Emergency PTT release failed: new TX blocked until radio release is confirmed");
      } else {
         cptr->is_ptt = false;
         cptr->ptt_vfo = 0;
         cptr->ptt_room[0] = '\0';
      }

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
