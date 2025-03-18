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

bool rr_amp_init(uint8_t index) {
   Log(LOG_INFO, "amp", " => Unit #%d initialized", index);

   return false;
}

bool rr_amp_init_all(void) {
   Log(LOG_INFO, "amp", "Initializing amplifiers");
   rr_amp_init(0);
   Log(LOG_INFO, "amp", "Amp setup complete");
   return false;
}
