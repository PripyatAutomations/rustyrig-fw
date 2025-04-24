//
// Amplifier module management
//
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
#include "ptt.h"
#include "thermal.h"
#include "protection.h"
#include "amp.h"
#include "logger.h"

#define	MAX_AMPS	4
typedef struct rr_amp_state {
   bool initialized;
   bool active;
} rr_amp_state_t;

rr_amp_state_t *amp_data[MAX_AMPS];

bool rr_amp_init(uint8_t index) {
   Log(LOG_INFO, "amp", " => Unit #%d initialized", index);
   if (index > (MAX_AMPS - 1)) {
      Log(LOG_CRIT, "amp", "rr_amp_init: got unit id %d > MAX_AMPS (%d), bailing!", index, MAX_AMPS);
      return true;
   }

   if (amp_data[index] == NULL) {
      amp_data[index] = malloc(sizeof(rr_amp_state_t));
      memset(amp_data[index], 0, sizeof(rr_amp_state_t));
   } else {
      Log(LOG_CRIT, "amp", "  * Unit %d already initialized at %x", index, amp_data[index]);
   }
   return false;
}

bool rr_amp_init_all(void) {
   Log(LOG_INFO, "amp", "Initializing amplifiers");
   rr_amp_init(0);
   Log(LOG_INFO, "amp", "Amp setup complete");
   return false;
}
