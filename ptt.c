//
// ptt.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Handle PTT and all interlocks preventing it's use
 *
 * we also deal with the PA_INHIBIT lines which allow momentarily stopping RF output without
 * powering down the PAs (such as for relay changes in tuning or filters).
 */
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "inc/logger.h"
#include "inc/state.h"
#include "inc/ptt.h"

bool rr_ptt_check_blocked(void) {
    if (rig.tx_blocked) {
       return true;
     }
     return false;
}

bool rr_ptt_set_blocked(bool blocked) {
   Log(LOG_AUDIT, "ptt", "PTT %sBLOCKED", (blocked ? "" : "un"));
   rig.tx_blocked = blocked;
   return blocked;
}

// For CAT to call
bool rr_ptt_set(rr_vfo_t vfo, bool ptt) {
   if (rr_ptt_check_blocked()) {
      Log(LOG_WARN, "ptt", "PTT request while blocked, ignoring!");
      return false;
   }

   if (rig.backend && rig.backend->api) {
      rig.backend->api->rig_ptt_set(vfo, ptt);
   } else {
      Log(LOG_WARN, "ptt", "no backend");
   }
   return ptt;
}

bool rr_ptt_toggle(rr_vfo_t vfo) {
   return rr_ptt_set(vfo, !rig.ptt);
}

bool rr_ptt_set_all_off(void) {
   Log(LOG_CRIT, "core", "PTT turned off for all VFOs!");
   rr_ptt_set(VFO_A, false);
   rr_ptt_set(VFO_B, false);
   rr_ptt_set(VFO_C, false);
   rr_ptt_set(VFO_D, false);
   rr_ptt_set(VFO_E, false);

   return false;
}
