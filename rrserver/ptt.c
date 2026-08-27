//
// rrserver/ptt.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
/*
 * Handle PTT and all interlocks preventing it's use
 *
 * we also deal with the PA_INHIBIT lines which allow momentarily stopping RF output
 * without powering down the PAs (such as for relay changes in tuning or filters).
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
#include <rrserver/ptt.h>
#include <rrserver/timer.h>

extern struct GlobalState rig;          // Global state
extern time_t ptt_tot_time;

time_t global_tot_time = 0;              // TOT
int vfos_enabled = 2;                    // A + B by default

bool rr_ptt_check_blocked(void) {
   if (rig.tx_blocked) {
      return true;
   }

   return false;
}

bool rr_ptt_set_blocked(bool blocked) {
   Log( LOG_AUDIT, "ptt", "PTT %sBLOCKED", (blocked ? "" : "un") );
   rig.tx_blocked = blocked;

   return blocked;
}

// For CAT to call
bool rr_ptt_set(rr_vfo_t vfo, bool ptt) {
   char msgbuf[HTTP_WS_MAX_MSG + 1];

   if ( rr_ptt_check_blocked() ) {
      Log(LOG_WARN, "ptt", "PTT request while blocked, ignoring!");

      return false;
   }

   // set or clear the talk timeout
   if (ptt) {
      global_tot_time = now + cfg_rig_hard_tot;
   } else {
      global_tot_time = 0;
   }

   if (rig.backend && rig.backend->api) {
      rig.backend->api->ptt_set(vfo, ptt);
   } else {
      Log(LOG_WARN, "ptt", "no backend");
   }

   const char *mode_str = rig.backend->api->mode_get_str(vfo);
   const char *jp = NULL;
   dict *d = dict_new();
   dict_add(d, "cat.state.vfo", vfo_name(vfo));
   dict_add(d, "cat.state.mode", mode_str);
   dict_add_bool(d, "cat.state.ptt", ptt);
   dict_add_int(d, "cat.state.freq", hl_state.freq);
   dict_add_int(d, "cat.state.width", hl_state.width);
   dict_add_ulong(d, "cat.state.ts", now);
   ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return ptt;
}

bool rr_ptt_toggle(rr_vfo_t vfo) {
   return rr_ptt_set(vfo, !rig.ptt);
}

bool rr_ptt_set_all_off(void) {
   Log(LOG_AUDIT, "core", "PTT turned off for all VFOs!");

   for (int i = 1 ; i < vfos_enabled ; i++) {
      rr_ptt_set(i, false);
   }

   global_tot_time = 0;

   return false;
}
