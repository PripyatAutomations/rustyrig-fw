#if	!defined(_logger_h)
#define	_logger_h
#include <stdarg.h>
enum LogPriority {
      CRIT = 0,
      WARN,
      INFO,
      DEBUG,
};
typedef enum LogPriority logpriority_t;
extern void Log(logpriority_t priority, const char *fmt, ...);
extern void logger_setup(void);
extern void logger_init(void);

#endif
