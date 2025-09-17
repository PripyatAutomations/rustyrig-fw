//
// logger.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * support logging to a a few places
 *	Targets: syslog console flash (file)
 */
//
// XXX: Need to implement filtering like this:
// XXX:	core.log-level=*:crit audio:crit core:debug ws:debug gtk:crit gtk.editserver:crazy
// XXX: where core.* and core mean the same. etc

#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include "librustyaxe/logger.h"
#include "librustyaxe/client-flags.h"

/* This should be updated only once per second, by a call to update_timestamp from main thread */
// These are in main
extern char latest_timestamp[64];
extern time_t now;
static time_t last_ts_update;

// Do we need to show a timestamp in log messages?
static bool log_show_ts = false;
int log_level = LOG_CRAZY;	// Default to showing ALL logging
char latest_timestamp[64];	// Current printed timestamp

static struct log_callback *log_callbacks = NULL;

/* String constants we use more than a few times */
const char s_prio_none[] = " NONE";

static struct log_priority log_priorities[] = {
   { .prio = LOG_AUDIT,	.msg = "audit" },
   { .prio = LOG_CRIT,	.msg = "crit" },
   { .prio = LOG_WARN,	.msg = "warn" },
   { .prio = LOG_INFO,	.msg = "info" },
   { .prio = LOG_DEBUG,	.msg = "debug" },
   { .prio = LOG_CRAZY, .msg = "crazy" },
   { .prio = LOG_NONE,	.msg = s_prio_none }		// invalid entry
};

FILE	*logfp = NULL;

enum LogPriority log_priority_from_str(const char *priority) {
   int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));

   for (int i = 0; i < log_levels; i++) {
      if (strcasecmp(log_priorities[i].msg, priority) == 0) {
         return log_priorities[i].prio;
      }
   }
   return LOG_NONE;
}


const char *log_priority_to_str(logpriority_t priority) {
   int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));

   for (int i = 0; i < log_levels; i++) {
      if (log_priorities[i].prio == priority) {
         return log_priorities[i].msg;
      }
   }
   return s_prio_none;
}

void logger_init(const char *logfile) {
   const char *ll = NULL;
#if	defined(USE_EEPROM)
   ll = eeprom_get_str("debug/loglevel");
   log_show_ts = eeprom_get_bool("debug/show_ts");
#endif

   if (!ll) {
      ll = cfg_get("log.level");
      log_show_ts = cfg_get_bool("log.show-ts", false);
   }

   if (ll) {
      int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));
      for (int i = 0; i < log_levels; i++) {
         if (strcasecmp(log_priorities[i].msg, ll) == 0) {
            log_level = log_priorities[i].prio;
            Log(LOG_INFO, "core", "Setting log_level to %d (%s)", log_priorities[i].prio, log_priorities[i].msg);
            break;
         }
      }
   } else {
      log_level = LOG_DEBUG;
   }

   if (!logfp) {
      logfp = fopen(logfile, "a+");

      if (!logfp) {
         Log(LOG_CRIT, "core", "Couldn't open log file at %s - %d:%s", logfile, errno, strerror(errno));
         return;
      }
   }
}

void logger_end(void) {
   if (logfp) {
      fclose(logfp);
   }
}

int update_timestamp(void) {
      time_t t;
   struct tm *tmp;

   // If we've already updated this second or have gone back in time, return success, to save CPU
   if (last_ts_update >= now) {
      return 0;
   }

   last_ts_update = now;
   memset(latest_timestamp, 0, sizeof(latest_timestamp));
   t = time(NULL);

   if ((tmp = localtime(&t))) {
      /* success, proceed */
      if (strftime(latest_timestamp, sizeof(latest_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         /* handle the error */
         memset(latest_timestamp, 0, sizeof(latest_timestamp));
         snprintf(latest_timestamp, sizeof(latest_timestamp), "<%ld>", (long)time(NULL));
      }
   } else {
      return 1;
   }
   return 0;
}

void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   char msgbuf[513];
   char ts_log_msg[1025];
   char log_msg[769];
   va_list ap, ap_c1;

   va_start(ap, fmt);
   if (!subsys || !fmt) {
      fprintf(stderr, "Invalid Log request: No subsys/fmt\n");
      va_end(ap);
      return;
   }

   if (priority > log_level) {
      return;
   }

   if (!debug_filter(subsys, priority)) {
      return;
   }

   if (logfp == NULL) {
      logfp = stdout;
   }

   // If this is a debug message, apply debug filtering
   // XXX: Finish this bit someday or remove it
/*
   if (priority == LOG_DEBUG) {
      if (debug_filter(subsys, fmt) == false) {
         // if we get false back, drop the message
         return;
      }
   }
*/

   // this is arranged so that it will return if called more than once a second
   if (log_show_ts) {
      update_timestamp();
   }

   /* clear the message buffer */
   memset(msgbuf, 0, sizeof(msgbuf));
   va_copy(ap_c1, ap);

   /* Expand the format string */
   vsnprintf(msgbuf, 511, fmt, ap);
   memset(log_msg, 0, sizeof(log_msg));
   snprintf(log_msg, sizeof(log_msg), "<%s.%s> %s", subsys, log_priority_to_str(priority), msgbuf);
   va_end(ap);

   /* Only spew to the serial port if logfile is closed */
   if (logfp) {
      if (log_show_ts) {
         fprintf(logfp, "[%s] %s\n", latest_timestamp, log_msg);
         if (logfp != stdout && logfp != stderr) {
            fprintf(stdout, "x[%s] %s\n", latest_timestamp, log_msg);
         }
      } else {
         fprintf(logfp, "%s\n", log_msg);
         if (logfp != stdout && logfp != stderr) {
            fprintf(stdout, "x: %s\n", log_msg);
         }
      }
      fflush(logfp);
   }

   // if there are registered log callbacks, call them
   if (log_callbacks) {
      struct log_callback *lp = log_callbacks;
      while (lp) {
         if (lp->callback) {
            lp->callback(fmt, ap_c1);
         }
         lp = lp->next;
      }
   }
   va_end(ap_c1);
}

bool log_remove_callback(struct log_callback *log_callback) {
   struct log_callback *prev = NULL, *i = log_callbacks;
   while (i) {
      if (i == log_callback) {
         prev->next = i;
         free(i);
         break;
      }
      prev = i;
      i = i->next;
   }
   return false;
}

bool log_add_callback(bool (*log_va_cb)(const char *fmt, va_list ap)) {
   struct log_callback *newcb = malloc(sizeof(struct log_callback));

   if (!newcb) {
      fprintf(stderr, "OOM in log_set_callback!\n");
      return true;
   }
   memset(newcb, 0, sizeof(struct log_callback));
   newcb->callback = log_va_cb;

   if (log_callbacks) {
      // find the end
      struct log_callback *prev = NULL;
      struct log_callback *i = log_callbacks;

      while (i) {
         // continue the loop
         prev = i;
         i = i->next;
      }

      if (prev) {
         prev->next = newcb;
      }
   } else {
      // first entry, pop it at the top of the list
      log_callbacks = newcb;
   }
   return false;
}
