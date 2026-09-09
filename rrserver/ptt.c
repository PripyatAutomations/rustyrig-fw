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
#ifdef	USE_SQLITE
#include <rrserver/database.h>
#endif

extern struct GlobalState rig;          // Global state
extern time_t ptt_tot_time;

time_t global_tot_time = 0;              // TOT
int vfos_enabled = 2;                    // A + B by default

// VFO currently keyed by PTT logging (row id in ptt_log, -1 none)
static int ptt_log_session[MAX_VFOS];

// Snapshot the VFO state and open a ptt_log row for the talker
static void ptt_log_start(rrconn_t *talker, rr_vfo_t vfo) {
#ifdef	USE_SQLITE
   if (!talker || !masterdb || vfo < 0 || vfo >= MAX_VFOS) {
      return;
   }
   if (ptt_log_session[vfo] > 0) {
      return;   // already logging this VFO
   }

   float power = rr_get_power(vfo);
   // Read state from the vfos[] table (polled from the active backend), not
   // the hamlib cache: other backends (internal) don't maintain hl_state.
   int session = db_ptt_start(masterdb, talker->chatname, vfo_name(vfo),
      (double)vfos[vfo].freq, vfo_mode_name(rr_get_mode(vfo)), (int)rr_get_width(vfo),
      power, "");

   if (session < 0) {
      Log(LOG_WARN, "ptt", "PTT log: failed to start session for %s", talker->chatname);
      return;
   }
   ptt_log_session[vfo] = session;
   talker->ptt_session = session;
   Log(LOG_DEBUG, "ptt", "PTT log: session %d opened for %s on VFO %s @ %.0f Hz",
      session, talker->chatname, vfo_name(vfo), (double)vfos[vfo].freq);
#else
   (void)talker;
   (void)vfo;
#endif
}

// Close the ptt_log row for the VFO, log how long they transmitted
// NB: `talker' may be NULL (e.g. is_ptt already cleared before the key-up
// reaches us, or TOT/fault/disconnect forced TX off). The per-VFO session
// table is the source of truth; the talker is only a fallback lookup.
static void ptt_log_stop(rrconn_t *talker, rr_vfo_t vfo) {
#ifdef	USE_SQLITE
   if (!masterdb || vfo < 0 || vfo >= MAX_VFOS) {
      return;
   }
   int session = ptt_log_session[vfo];

   if (session <= 0 && talker) {
      session = talker->ptt_session;   // fallback
   }
   if (session <= 0) {
      return;
   }
   ptt_log_session[vfo] = -1;
   if (talker) {
      talker->ptt_session = 0;
   }

   int secs = -1;

   if (!db_ptt_stop(masterdb, session, &secs) ) {
      Log(LOG_WARN, "ptt", "PTT log: failed to close session %d for %s", session, (talker ? talker->chatname : "unknown") );
      return;
   }
   if (secs >= 0) {
      Log(LOG_INFO, "ptt", "PTT log: %s was on the air for %d seconds (session %d)",
         (talker ? talker->chatname : "unknown"), secs, session);
   }
#else
   (void)talker;
   (void)vfo;
#endif
}

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
   // Config: rig.tot - max TX time in seconds (default 300) before the
   // clocktick timer halts PTT. PARITY: rrserver/timer.clocktick.c TOT check
   if (ptt) {
      global_tot_time = now + cfg_get_int("rig.tot", 300);
   } else {
      global_tot_time = 0;
   }

   // PTT logging: snapshot VFO state on key-down; log TX seconds on key-up.
   // whos_talking() is updated by librrprotocol (cptr->is_ptt) before the
   // rigctl event reaches us, so the talker is already current here.
   if (vfo >= 0 && vfo < MAX_VFOS) {
      rrconn_t *talker = whos_talking();

      if (ptt && talker) {
         ptt_log_start(talker, vfo);
      } else if (!ptt) {
         // Close the session for whoever was on this VFO (may be gone by now
         // if TOT/fault/disconnect forced TX off)
         ptt_log_stop(talker, vfo);
      }
   }

   // Go through backend.c (rr_ptt_apply) rather than poking the backend api
   // directly. PARITY: rrserver/backend.c rr_ptt_apply()
   // NB: rr_ptt_apply() returns false on SUCCESS, true on failure.
   if (rr_ptt_apply(vfo, ptt) ) {
      Log(LOG_WARN, "ptt", "Failed to apply PTT %s (no backend or backend error?)", (ptt ? "ON" : "OFF") );
   }

   // Broadcast immediate cat.state so clients see TX state without waiting
   // for the next backend poll.
   if (rig.backend && rig.backend->api && rig.backend->api->mode_get_str) {
      const char *mode_str = rig.backend->api->mode_get_str(vfo);
      dict *d = dict_new();
      dict_add(d, "msg.type", "cat.state");
      dict_add(d, "cat.state.vfo", vfo_name(vfo) );
      dict_add(d, "cat.state.mode", mode_str);
      dict_add_bool(d, "cat.state.ptt", ptt);
      // Read freq/width from the vfos[] table (polled from the active backend);
      // hl_state is a hamlib-only cache and is zeroed for other backends.
      dict_add_int(d, "cat.state.freq", (vfo >= 0 && vfo < MAX_VFOS ? vfos[vfo].freq : 0) );
      dict_add_int(d, "cat.state.width", (vfo >= 0 && vfo < MAX_VFOS ? vfos[vfo].width : 0) );
      dict_add_ulong(d, "msg.ts", now);
      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   }

   return ptt;
}

bool rr_ptt_toggle(rr_vfo_t vfo) {
   return rr_ptt_set(vfo, !rig.ptt);
}

bool rr_ptt_set_all_off(void) {
   Log(LOG_AUDIT, "core", "PTT turned off for all VFOs!");

   for (int i = VFO_A ; i < MAX_VFOS ; i++) {
      rr_ptt_set((rr_vfo_t)i, false);
   }

   global_tot_time = 0;

   return false;
}
