/*
 * support logging to a a few places
 *	Targets: syslog console flash (file)
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "logger.h"
#include "state.h"
#include "eeprom.h"
#include "debug.h"		// Debug message filtering

/* This should be updated only once per second, by a call to update_timestamp from main thread */
// These are in main
extern char latest_timestamp[64];
extern time_t now;
static time_t last_ts_update;

// Do we need to show a timestamp in log messages?
static bool log_show_ts = false;

/* String constants we use more than a few times */
const char s_prio_none[] = " NONE";

static struct log_priority log_priorities[] = {
   { .prio = LOG_CRIT,	.msg = "crit" },
   { .prio = LOG_WARN,	.msg = "warn" },
   { .prio = LOG_INFO,	.msg = "info" },
   { .prio = LOG_AUDIT,	.msg = "audit" },
   { .prio = LOG_DEBUG,	.msg = "debug" },
   { .prio = LOG_NONE,	.msg = s_prio_none }		// invalid entry
};

FILE	*logfp = NULL;

const char *log_priority_to_str(logpriority_t priority) {
   int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));

   for (int i = 0; i < log_levels; i++) {
      if (log_priorities[i].prio == priority) {
         return log_priorities[i].msg;
      }
   }
   return s_prio_none;
}

void logger_init(void) {
   const char *ll = eeprom_get_str("debug/loglevel");

   if (ll == NULL) {
      printf("logger_init: Failed to get loglevel from eeprom");
      return;
   }

   log_show_ts = eeprom_get_bool("debug/show_ts");

   int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));
   for (int i = 0; i < log_levels; i++) {
      if (strcasecmp(log_priorities[i].msg, ll) == 0) {
         rig.log_level = log_priorities[i].prio;
         Log(LOG_INFO, "core", "Setting log_level to %d (%s) = %s", log_priorities[i].prio, log_priorities[i].msg, log_priority_to_str(log_priorities[i].prio));
         break;
      }
   }

#if	defined(HOST_POSIX)
/* This really should be HAVE_FS or such, rather than HOST_POSIX as we could log to SD, etc... */
   if (logfp == NULL) {
      logfp = fopen(LOG_FILE, "a+");

      if (logfp == NULL) {
         return;
      }
      // XXX: do final setup
   }
#else
/* Machdep log setup goes here */
#endif
}

void logger_end(void) {
#if	defined(HOST_POSIX)
/* This really should be HAVE_FS or such, rather than HOST_POSIX as we could log to SD, etc... */
   if (logfp != NULL)
      fclose(logfp);
#else
/* Machdep log teardown goes here */
#endif
}

static inline int update_timestamp(void) {
   time_t t;
   struct tm *tmp;

   // If we've already updated this second or have gone back in time, return success, to save CPU
   if (last_ts_update >= now) {
      return 0;
   }

   last_ts_update = now;
   memset(latest_timestamp, 0, sizeof(latest_timestamp));
   t = time(NULL);

   if ((tmp = localtime(&t)) != NULL) {
      /* success, proceed */
      if (strftime(latest_timestamp, sizeof(latest_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         /* handle the error */
         memset(latest_timestamp, 0, sizeof(latest_timestamp));
         snprintf(latest_timestamp, sizeof(latest_timestamp), "<%lu>", time(NULL));
      }
   } else {
      return 1;
   }
   return 0;
}

void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   char msgbuf[512];
   va_list ap;

   if (subsys == NULL || fmt == NULL) {
      printf("Invalid Log request: No subsys/fmt\n");
      return;
   }

   // If this is a debug message, apply debug filtering
   if (priority == LOG_DEBUG) {
      if (debug_filter(subsys, fmt) == false) {
         // if we get false back, drop the message
         return;
      }
   }

   // update the timestamp string
   if (log_show_ts) {
      update_timestamp();
   }

   /* clear the message buffer */
   memset(msgbuf, 0, sizeof(msgbuf));

   va_start(ap, fmt);

   /* Expand the format string */
   vsnprintf(msgbuf, 511, fmt, ap);

#if	defined(HOST_POSIX) || defined(FEATURE_FILESYSTEM)
   /* Only spew to the serial port if logfile is closed */
   if (logfp == NULL && logfp != stdout) {
      // XXX: this should support network targets too!!
//      cons_printf("%s %s: %s\n", latest_timestamp, log_priority_to_str(priority), msgbuf);
      va_end(ap);
      return;
   } else {
      if (log_show_ts) {
         fprintf(logfp, "[%s] <%s.%s> %s\n", latest_timestamp, log_priority_to_str(priority), subsys, msgbuf);
      } else {
         fprintf(logfp, "<%s.%s> %s\n", log_priority_to_str(priority), subsys, msgbuf);
      }
      fflush(logfp);
   }
#endif

#if	defined(HOST_POSIX)
   if (logfp != stdout) {
      // Send it to the stdout too on host builds
      if (log_show_ts) {
         fprintf(stdout, "[%s] <%s.%s> %s\n", latest_timestamp, log_priority_to_str(priority), subsys, msgbuf);
      } else {
         fprintf(stdout, "<%s.%s> %s\n", log_priority_to_str(priority), subsys, msgbuf);
      }
   }
#endif

   /* Machdep logging goes here! */

   va_end(ap);
}

void hash_to_hex(char *dest, const uint8_t *hash, size_t len) {
   if (dest == NULL || hash == NULL || len <= 0) {
      return;
   }

   for (size_t i = 0; i < len; i++) {
      sprintf(dest + (i * 2), "%02x", hash[i]);
   }

   dest[len * 2] = '\0';
}
