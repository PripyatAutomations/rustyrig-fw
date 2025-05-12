//
// protection.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we deal with SWR protection
//
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
#include "inc/protection.h"

extern struct GlobalState rig;      // Global state

// Warmup protection
bool rr_protect_warmup_pending(int amp_idx) {
   if (amp_idx < 0) {
      return false;
   }

   // Lookup warmup-required and warmup-time for the passed amp

   // Return no, no warmup needed
   return false;
}

bool protection_lockout(const char *reason) {
   rig.tx_blocked = true;
   return false;
}
