#if	!defined(__rr_protection_h)
#define	__rr_protecion_h
#include "config.h"

extern bool rr_protect_warmup_pending(int amp_idx);
extern bool protection_lockout(const char *reason);

#endif	// !defined(__rr_protection_h)
