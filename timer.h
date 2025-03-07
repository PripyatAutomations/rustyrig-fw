#if	!defined(__rr_timer_h)
#define	__rr_timer_h
#include "config.h"

extern bool timer_create_periodic(int interval, int repeats, void (*callback)());
extern bool timer_create_oneshot(int delay, void (*callback)());

#endif	// !defined(__rr_timer_h)
