#if	!defined(__rr_posix_h)
#define	__rr_posix_h
#include <stdbool.h>

extern void host_cleanup(void);
extern bool host_init(void);
extern bool file_exists(const char *path);

#endif	// !defined(__rr_posix_h)
