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
#include "inc/auth.h"
#include "inc/ws.h"
#include "inc/http.h"

time_t   global_tot_time = 0;		// TOT

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

   // set or clear the talk timeout
   if (ptt) {
      global_tot_time = now + RF_TALK_TIMEOUT;
   } else {
      global_tot_time = 0;
   }

   if (rig.backend && rig.backend->api) {
      rig.backend->api->rig_ptt_set(vfo, ptt);
   } else {
      Log(LOG_WARN, "ptt", "no backend");
   }

   // turn off PTT flag on any user that may hold it
   http_client_t *curr = http_client_list;
   while (curr != NULL) {
      // if set, clear the ptt flag for user and announce it
      if (curr->is_ptt) {
         curr->is_ptt = false;

         // Update the userlist
         ws_send_userinfo(curr, NULL);
      }
      curr = curr->next;
   }

   // send an alert that TOT expired
   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf), "TOT expired, halting TX!");
   send_global_alert("***SERVER***", msgbuf);

   // XXX: fix this someday? we'll have to for multi-vfo modes
//   hl_state.ptt = false;

   // and send a CAT message turning off ptt
   prepare_msg(msgbuf, sizeof(msgbuf), "{ \"cat\": { \"state\": { \"vfo\": \"%c\", \"freq\": %f, \"mode\": \"%s\", \"width\": %d, \"ptt\": \"%s\" }, \"ts\": %lu  } }",
       vfo_name(vfo), (hl_state.freq), rig_strrmode(hl_state.rmode), hl_state.width, (ptt ? "true" : "false"), now);
   struct mg_str mp = mg_str(msgbuf);
   ws_broadcast(NULL, &mp);
   Log(LOG_DEBUG, "ptt", "sending ptt state msg: %s", msgbuf);

   return ptt;
}

// XXX: This needs updated to support multiple VFOs
bool rr_ptt_toggle(rr_vfo_t vfo) {
   return rr_ptt_set(vfo, !rig.ptt);
}

bool rr_ptt_set_all_off(void) {
   Log(LOG_AUDIT, "core", "PTT turned off for all VFOs!");
   rr_ptt_set(VFO_A, false);
   rr_ptt_set(VFO_B, false);
//   rr_ptt_set(VFO_C, false);
//   rr_ptt_set(VFO_D, false);
//   rr_ptt_set(VFO_E, false);

   global_tot_time = 0;

   return false;
}
