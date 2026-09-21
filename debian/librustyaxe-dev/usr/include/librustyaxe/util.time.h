//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__util_time_h)
#define	__util_time_h
#include <time.h>
#include <stdbool.h>

extern const char *get_chat_ts(time_t ts);
extern time_t dhms2time_t(const char *str);
extern char *time_t2dhms(time_t seconds);
extern void format_timestamp(time_t t, char *buf, size_t buflen);
extern long long timespec_diff_ms(const struct timespec *a, const struct timespec *b);

// Monotonic milliseconds since an arbitrary epoch: safe for latency
// measurement (immune to wall-clock jumps), wraps ~292k years
extern long long mono_ms(void);

// Monotonic microseconds since an arbitrary epoch: real-usec resolution for
// RTT measurement (immune to wall-clock jumps)
extern long long mono_us(void);

#endif // !defined(__util_time_h)
