//
// logger.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_common_logger_h)
#define	__rr_common_logger_h
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <librustyaxe/config.h>

enum LogPriority {
      LOG_NONE = -1,
      LOG_CRIT,			// Auditing events
      LOG_AUDIT,		// Critical problems
      LOG_WARN,			// Warnings
      LOG_INFO,			// Useful information of a non-important nature
      LOG_DEBUG,		// Most commonly useful for debugging problems, a balance of verbosity and speed
      LOG_CRAZY,		// Many usually useless messages, which can aid with debugging but too noisy for typical debugging use
      LOG_BLITZKREIG		// unmanagably noisy and will slow the program greatly - for debugging only!
};

struct log_priority {
   enum LogPriority	prio;
   const char 		*msg;
};
typedef enum LogPriority logpriority_t;

struct log_callback {
   enum LogPriority	 prio;
   const char           *msg;
   bool                (*callback)(logpriority_t priority, const char *subsys, const char *fmt, va_list ap);
   struct log_callback *next;
};

extern FILE *logfp;
extern int log_level;
extern void Log(logpriority_t priority, const char *subsys, const char *fmt, ...);
extern void logger_setup(void);
extern void logger_init(const char *logfile);
extern void hash_to_hex(char *dest, const uint8_t *hash, size_t len);
extern char latest_timestamp[64];
extern int update_timestamp(void);
extern enum LogPriority log_priority_from_str(const char *priority);
extern void logger_end(void);

// Add a callback to the Log() call
extern bool log_add_callback(bool (*log_va_cb)(logpriority_t priority, const char *subsys,  const char *fmt, va_list ap));
extern bool log_remove_callback(struct log_callback *log_callback);

// Filters
extern bool log_add_filter(const char *pattern, logpriority_t level);
extern void log_clear_filters(void);
extern bool debug_filter(const char *subsys, logpriority_t msg_level);

#endif	// !defined(__rr_common_logger_h)
