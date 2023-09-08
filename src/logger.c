/*
 * support logging to a a few places
 *	Targets: syslog console flash (file)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "config.h"
#include "logger.h"
#include "state.h"
#include "parser.h"

extern struct GlobalState rig;	// Global state

/* String constants we use more than a few times */
const char *s_prio_crit = "CRIT",
           *s_prio_warn = "WARN",
           *s_prio_info = "INFO",
           *s_prio_debug = "DEBUG",
           *s_none = "NONE";

FILE   *logfp = NULL;

const char *log_priority_to_str(logpriority_t priority) {
   switch (priority) {
      case CRIT:
         return s_prio_crit;
         break;
      case WARN:
         return s_prio_warn;
         break;
      case INFO:
         return s_prio_info;
         break;
      case DEBUG:
         return s_prio_info;
         break;
      default:
         return s_none;
         break;
   }
   return NULL;
}

void logger_init(void) {
#if	defined(HOST_BUILD)
   if (logfp == NULL) {
      logfp = fopen(HOST_LOG_FILE, "a+");

      if (logfp == NULL) {
         return;
      }
      // XXX: do final setup
   }
#endif
}

void logger_end(void) {
#if	defined(HOST_BUILD)
   if (logfp != NULL)
      fclose(logfp);
#endif
}

void Log(logpriority_t priority, const char *fmt, ...) {
   char msgbuf[512];
   char tsbuf[64];
   va_list ap;
   time_t t;
   struct tm *tmp;

   /* clear the message buffer */
   memset(msgbuf, 0, sizeof(msgbuf));

   va_start(ap, fmt);

   /* Expand the format string */
   vsnprintf(msgbuf, 511, fmt, ap);

   memset(tsbuf, 0, sizeof(tsbuf));
   t = time(NULL);

   if ((tmp = localtime(&t)) != NULL) {
      /* success, proceed */
      if (strftime(tsbuf, sizeof(tsbuf), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         /* handle the error */
         memset(tsbuf, 0, sizeof(tsbuf));
         snprintf(tsbuf, sizeof(tsbuf), "unknown<%lu>", time(NULL));
      }
   }

   /* Only spew to the serial port if syslog and logfile are closed */
   if (logfp == NULL) {
//      Serial.printf("[%s] %s: %s\n", log_priority_to_str(priority), tsbuf, msgbuf);
      va_end(ap);
      return;
   }

   /* log to the logfile */
   if (logfp != NULL) {
      fprintf(logfp, "%s %s: %s\n", tsbuf, log_priority_to_str(priority), msgbuf);
      fflush(logfp);
   }

#if	defined(HOST_BUILD)
   // Send it to the stdout too on host builds
   fprintf(stdout, "[%s] %s: %s\n", tsbuf, log_priority_to_str(priority), msgbuf);
#endif	// defined(HOST_BUILD)

   va_end(ap);
}
