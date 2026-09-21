//
// util.mem.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Allocation wrappers: allocation failure is OOM and is treated as fatal.
// PARITY: keep behavior identical to Log(LOG_CRIT) + exit(ENOMEM) convention
// used in librustyaxe/ringbuffer.c and librustyaxe/subproc.c
#if     !defined(__rr_util_mem_h)
#define	__rr_util_mem_h

#include <stddef.h>

// Report an OOM (allocation failure) and terminate. `what` names the
// allocation site, e.g. "event_on listener".
extern void oom_fatal(const char *what);

// Wrappers which never return NULL; on failure they call oom_fatal()
extern void *xmalloc(size_t size);
extern void *xcalloc(size_t nmemb, size_t size);
extern void *xrealloc(void *ptr, size_t size);
extern char *xstrdup(const char *s);

#endif // !defined(__rr_util_mem_h)
