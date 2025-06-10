//
// protection.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_protection_h)
#define	__rr_protecion_h
#include "rustyrig/config.h"

extern bool rr_protect_warmup_pending(int amp_idx);
extern bool protection_lockout(const char *reason);

#endif	// !defined(__rr_protection_h)
