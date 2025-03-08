#if	!defined(__rr_logger_h)
#define	__rr_logger_h
#include "config.h"
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include "debug.h"

enum LogPriority {
      LOG_NONE = -1,
      LOG_CRIT,
      LOG_WARN,
      LOG_INFO,
      LOG_AUDIT,
      LOG_DEBUG,
};
struct log_priority {
   enum LogPriority	prio;
   const char 		*msg;
};

extern FILE *logfp;
typedef enum LogPriority logpriority_t;
extern void Log(logpriority_t priority, const char *subsys, const char *fmt, ...);
extern void logger_setup(void);
extern void logger_init(void);

#endif
