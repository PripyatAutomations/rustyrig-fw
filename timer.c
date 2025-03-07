/*
 * Here we implement timers (periodic and one-shot) in a platform
 * independent manner.
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "timer.h"

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
