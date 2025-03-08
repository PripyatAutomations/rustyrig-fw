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

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool amp_init(uint8_t index) {
   Log(LOG_INFO, "amp", " => Unit #%d initialized", index);

   // XXX: Set up CAT (kpa500), if enabled

   return false;
}

bool amp_init_all(void) {
   Log(LOG_INFO, "amp", "Initializing amplifiers");
   amp_init(0);
   Log(LOG_INFO, "amp", "Amp setup complete");
   return false;
}
