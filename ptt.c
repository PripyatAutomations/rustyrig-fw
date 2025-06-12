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
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "rustyrig/state.h"
#include "rustyrig/ptt.h"
#include "rustyrig/auth.h"
#include "rustyrig/ws.h"
#include "rustyrig/http.h"

time_t   global_tot_time = 0;		// TOT
int	 vfos_enabled = 2;		// A + B by default

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
   char msgbuf[HTTP_WS_MAX_MSG+1];

   if (rr_ptt_check_blocked()) {
      Log(LOG_WARN, "ptt", "PTT request while blocked, ignoring!");
      return false;
   }

   // set or clear the talk timeout
   if (ptt) {
      global_tot_time = now + RF_TALK_TIMEOUT;
   } else {
      global_tot_time = 0;
   }

   if (rig.backend && rig.backend->api) {
      rig.backend->api->ptt_set(vfo, ptt);
   } else {
      Log(LOG_WARN, "ptt", "no backend");
   }

   // and send a CAT message with the state
   prepare_msg(msgbuf, sizeof(msgbuf), "{ \"cat\": { \"state\": { \"vfo\": \"%c\", \"freq\": %f, \"mode\": \"%s\", \"width\": %d, \"ptt\": \"%s\" }, \"ts\": %lu  } }",
       vfo_name(vfo), (hl_state.freq), rig_strrmode(hl_state.rmode), hl_state.width, (ptt ? "true" : "false"), now);
   struct mg_str mp = mg_str(msgbuf);
   ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);

   return ptt;
}

bool rr_ptt_toggle(rr_vfo_t vfo) {
   return rr_ptt_set(vfo, !rig.ptt);
}

bool rr_ptt_set_all_off(void) {
   Log(LOG_AUDIT, "core", "PTT turned off for all VFOs!");
   for (int i = 1; i < vfos_enabled; i++) {
      rr_ptt_set(i, false);
   }

   global_tot_time = 0;

   return false;
}
