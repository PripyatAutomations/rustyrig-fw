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
extern struct GlobalState rig;	// Global state

bool ptt_check_blocked(void) {
    if (rig.tx_blocked) {
       return true;
     }
     return false;
}

bool ptt_set_blocked(bool blocked) {
   rig.tx_blocked = blocked;
   return blocked;
}


bool ptt_set(bool ptt) {
   if (ptt_check_blocked()) {
      Log(LOG_WARN, "PTT request while blocked");
      return false;
   }

   rig.ptt = ptt;
   return rig.ptt;
}

bool ptt_toggle(void) {
   return ptt_set(!rig.ptt);
}
