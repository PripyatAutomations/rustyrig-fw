//
// timer.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we implement timers (periodic and one-shot) in a platform
 * independent manner.
 */
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "rustyrig/state.h"
#include "rustyrig/timer.h"

// timer_create_periodic:
//	Create a timer that occurs every interval milliseconds
//	Repeats can be used to create a timer that only happens a few times
// 		Use repeats = 0 for unlimited repeats
//	callback
bool timer_create_periodic(int interval, int repeats, void (*callback)()) {
   return false;
}

bool timer_create_oneshot(int delay, void (*callback)()) {
   return false;
}

// Run all pending timers this iteration of the main loop
bool timer_run(void) {
   return false;
}

// initialize the timer subsystem
bool timer_init(void) {
   Log(LOG_INFO, "timer", "Initialized timers");
   return false;
}
