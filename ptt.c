/*
 * Handle PTT and all interlocks preventing it's use
 *
 * we also deal with the PA_INHIBIT lines which allow momentarily stopping RF output without
 * powering down the PAs (such as for relay changes in tuning or filters).
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "ptt.h"

bool ptt_check_blocked(void) {
    if (rig.tx_blocked) {
       return true;
     }
     return false;
}

bool ptt_set_blocked(bool blocked) {
   Log(LOG_AUDIT, "ptt", "PTT %sBLOCKED", (blocked ? "" : "un"));
   rig.tx_blocked = blocked;
   return blocked;
}

// For CAT to call
bool ptt_set(rr_vfo_t vfo, bool ptt) {
   if (ptt_check_blocked()) {
      Log(LOG_WARN, "ptt", "PTT request while blocked, ignoring!");
      return false;
   }

   rig.backend->api->rig_ptt_set(vfo, ptt);
   return ptt;
}

bool ptt_toggle(rr_vfo_t vfo) {
   return ptt_set(vfo, !rig.ptt);
}

bool ptt_set_all_off(void) {
   Log(LOG_CRIT, "core", "PTT turned off for all VFOs!");
   ptt_set(VFO_A, false);
   ptt_set(VFO_B, false);
   ptt_set(VFO_C, false);
   ptt_set(VFO_D, false);
   ptt_set(VFO_E, false);

   return false;
}
