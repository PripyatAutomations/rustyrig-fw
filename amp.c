//
// amp.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Amplifier module management
//
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "inc/state.h"
#include "inc/ptt.h"
#include "inc/thermal.h"
#include "inc/protection.h"
#include "inc/amp.h"
#include "inc/logger.h"

#define	MAX_AMPS	4
typedef struct rr_amp_state {
   bool initialized;
   bool active;
} rr_amp_state_t;

rr_amp_state_t *amp_data[MAX_AMPS];

bool rr_amp_init(uint8_t index) {
   if (index > (MAX_AMPS - 1)) {
      Log(LOG_CRIT, "amp", "rr_amp_init: got unit id %d > MAX_AMPS (%d), bailing!", index, MAX_AMPS);
      return true;
   }

   Log(LOG_INFO, "amp", " => Unit #%d initializing", index);

   if (amp_data[index] == NULL) {
      amp_data[index] = malloc(sizeof(rr_amp_state_t));
      memset(amp_data[index], 0, sizeof(rr_amp_state_t));
   } else {
      Log(LOG_CRIT, "amp", "  * Unit %d already initialized at %x", index, amp_data[index]);
   }
   return false;
}

bool rr_amp_init_all(void) {
   Log(LOG_INFO, "amp", "Initializing all amplifiers");
   rr_amp_init(0);
   Log(LOG_INFO, "amp", "Amp setup complete");
   return false;
}
