//
// posix.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_posix_h)
#define	__rr_posix_h
#include <stdbool.h>

extern void host_cleanup(void);
extern bool host_init(void);
extern bool file_exists(const char *path);

// if built for one of the apps
extern void shutdown_app(int signum);

#ifdef _WIN32
extern char *strndup(const char *s, size_t n);
#endif

#endif	// !defined(__rr_posix_h)
