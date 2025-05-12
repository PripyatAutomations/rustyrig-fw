//
// logger.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_logger_h)
#define	__rr_logger_h
#include "inc/config.h"
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include "inc/debug.h"

enum LogPriority {
      LOG_NONE = -1,
      LOG_CRIT,
      LOG_AUDIT,
      LOG_WARN,
      LOG_INFO,
      LOG_DEBUG,
      LOG_CRAZY
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
extern void hash_to_hex(char *dest, const uint8_t *hash, size_t len);

#endif
