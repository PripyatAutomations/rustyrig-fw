//
// timer.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we implement timers (periodic and one-shot) in a platform independent manner.
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
#include <rrserver/timer.h>

// timer_create_periodic:
// Create a timer that occurs every interval milliseconds
// Repeats can be used to create a timer that only happens a few times
//       Use repeats = 0 for unlimited repeats
bool timer_create_periodic( const char *name, int interval, int repeats, void (*callback) () ) {
#ifdef	USE_MONGOOSE
   // XXX: Provide a libmongoose based timer here
#else	// USE_MONGOOSE
#ifdef	USE_LIBEV
   // XXX: Provide a libev based timer here
#else	// USE_LIB_EV
#ifdef	HOST_POSIX
   // XXX: Implement fallback version using posix timers on linux/glibc and bsd if possible
#endif	// HOST_POSIX
#endif	// USE_LIBEV
#endif	// USE_MONGOOSE
   return false;
}

bool timer_create_oneshot( const char *name, int delay, void (*callback) () ) {
   return timer_create_periodic(name, delay, 1, callback);
}

// Run all pending timers this iteration of the main loop
bool timer_run(void) {
#if	!defined(USE_MONGOOSE) && !defined(USE_LIBEV) && defined(HOST_POSIX)
   // XXX: Add support for manually running timers in the main loop without a library on posix ;(
#endif
   return false;
}

// initialize the timer subsystem
bool timer_init(void) {
   Log(LOG_INFO, "timer", "Initialized timers");

   return false;
}
