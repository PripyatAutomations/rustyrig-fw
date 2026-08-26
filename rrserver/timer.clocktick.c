//
// rrserver/timer.clocktick.c: Periodic (1hz) timer
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

extern struct timespec loop_start;      // main.c
extern struct timespec mono_now;
extern int cfg_backend_announce_interval;	// main.c: how many times to skip announcing unless diff

// This is the hardware limit, not reconfigurable
const time_t cfg_rig_hard_tot = RF_TALK_TIMEOUT;
static time_t be_poll_last = 0;
static int clock_expire_http_iter = 0;
static int clock_expire_fwdsp_iter = 0;
static rr_vfo_data_t last_vfo_state[MAX_VFOS];

// Here we do the things that aren't terribly time sensitive, with about a 1hz interval
void timer_clock_tick_fn(void *arg) {
   now = time(NULL);

   // Check thermals
   if ( are_we_on_fire() ) {
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

   bool force_send_state = false;
   // Should we send rig state announcements?
   if ((be_poll_last + cfg_backend_announce_interval) <= now) {
      // Yes, we must send it
      force_send_state = true;
   }

   // save the vfo state for the next check
   for (int i = 0; i < MAX_VFOS; i++) {
      last_vfo_state[i] = vfos[i];
   }

//   if (force_send_state || vfo_diff(vfos[i], last_vfo_state[i])) {
   if (force_send_state) {
      // send a cat.state message
      // Update the last polled time
      be_poll_last = now;
   }
}
