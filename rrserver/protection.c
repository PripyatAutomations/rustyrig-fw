//
// protection.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we deal with SWR protection
//
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

extern struct GlobalState rig;       // Global state
extern time_t now, started;
bool warming_up = true;

////////
int cfg_warmup_time = 30;
bool cfg_warmup_required = true;

// Warmup protection. Can we transmit yet?
bool protection_warmup_pending(int amp_idx) {
   if (amp_idx < 0 || !warming_up || !cfg_warmup_required) {
      return false;
   }

   time_t uptime = (now - started);
   if (uptime < cfg_warmup_time) {
      return true;
   }

   // Return no, no warmup needed
   return false;
}

bool protection_lockout(const char *reason) {
   rig.tx_blocked = true;

   char timestamp[32];
   memset(timestamp, 0, sizeof(timestamp));
   snprintf(timestamp, sizeof(timestamp), "%lu", now);

   dict *d = dict_new();
   dict_add(d, "protection.reason", (char *)(reason ? reason : "No reason given"));
   dict_add(d, "protection.ts", timestamp);
   event_emit_dict("protection", NULL, d);
   dict_free(d);

   return false;
}

bool protection_init(void) {
   cfg_warmup_time = cfg_get_int("rig.warmup-time", 30);
   cfg_warmup_required = cfg_get_bool("rig.warmup-required", true);
   return false;
}