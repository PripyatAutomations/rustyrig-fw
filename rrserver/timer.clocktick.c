//
// main.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/faults.h>
#include <rrserver/ptt.h>
#include <rrserver/thermal.h>
#include <rrserver/backend.h>

extern struct timespec loop_start;	// main.c

// This is the hardware limit, not reconfigurable
const time_t cfg_rig_hard_tot = RF_TALK_TIMEOUT;

static int clock_expire_http_iter = 0;
static int clock_expire_fwdsp_iter = 0;

void timer_clock_tick_fn(void *arg) {
   // Update our time keeping variables once per second
   clock_gettime(CLOCK_MONOTONIC, &loop_start);
   now = time(NULL);

   // Check thermals
   if (are_we_on_fire() ) {
      rr_ptt_set_all_off();
      rr_ptt_set_blocked(true);
      Log(LOG_CRIT, "core", "Radio is on fire?! Halted TX!");
   }

   // Has the TOT expired?
   if (global_tot_time > 0 && global_tot_time <= now) {
      rrconn_t *talker = whos_talking();
      Log(LOG_AUDIT, "ptt", "TOT (%d) expired, halting TX!", cfg_rig_hard_tot);
      rr_ptt_set_all_off();
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg( msgbuf, sizeof(msgbuf), "TOT expired, halting TX! PTT User: %s",
         (talker ? talker->chatname : "**UNKNOWN***") );
      send_global_alert("***SERVER***", msgbuf);
      global_tot_time = 0;
   }

   // Send pings, drop dead connections, etc

   // Only expire fwdsp sessions every 10 seconds
   if (clock_expire_fwdsp_iter >= 30) {
      // deal with timed out en/decoders
//      fwdsp_sweep_expired();
      clock_expire_fwdsp_iter = 0;
   } else {
      clock_expire_fwdsp_iter++;
   }

   // Only expire http sessions every third seconds
   if (clock_expire_http_iter >= 30) {
      http_expire_sessions();
      clock_expire_http_iter = 0;
   } else {
      clock_expire_http_iter++;
   }
}
