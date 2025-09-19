#if	!defined(__util_time_h)
#define __util_time_h
#include <time.h>
#include <stdbool.h>

extern const char *get_chat_ts(time_t curr);
extern time_t dhms2time_t(const char *str);
extern char *time_t2dhms(time_t seconds);

#endif	// !defined(__util_time_h)
