//
// timer.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_timer_h)
#define	__rr_timer_h
#include "rustyrig/config.h"
#include "rustyrig/mongoose.h"

extern bool timer_create_periodic(int interval, int repeats, void (*callback)());
extern bool timer_create_oneshot(int delay, void (*callback)());
extern bool timer_run(void);
extern bool timer_init(void);

#endif	// !defined(__rr_timer_h)
