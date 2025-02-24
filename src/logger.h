#if	!defined(__rr_logger_h)
#define	__rr_logger_h
#include "config.h"
#include <stdarg.h>

enum LogPriority {
      LOG_CRIT = 0,
      LOG_WARN,
      LOG_INFO,
      LOG_DEBUG,
};
typedef enum LogPriority logpriority_t;
extern void Log(logpriority_t priority, const char *fmt, ...);
extern void logger_setup(void);
extern void logger_init(void);

#endif
