#if	!defined(__rr_timer_h)
#define	__rr_timer_h
#include "config.h"
#include "mongoose.h"

extern bool timer_create_periodic(int interval, int repeats, void (*callback)());
extern bool timer_create_oneshot(int delay, void (*callback)());
extern bool timer_run(void);
extern bool timer_init(void);

#endif	// !defined(__rr_timer_h)
