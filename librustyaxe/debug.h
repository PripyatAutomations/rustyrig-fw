//
// debug.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_debug_h)
#define	__rr_debug_h
#include <stdbool.h>
#include <stdint.h>

extern bool debug_init(void);
extern bool debug_filter(const char *subsys, logpriority_t level);

#endif	// !defined(__rr_debug_h)
