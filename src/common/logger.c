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
#include "common/config.h"
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
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"

// We don't build these for gtk client or fwdsp
#if	!defined(__RRCLIENT) && !defined(__FWDSP)
#include "rustyrig/eeprom.h"
#include "common/debug.h"			// Debug message filtering
#include "rustyrig/ws.h"			// Support for sending the syslog via websocket
#include "common/client-flags.h"
#endif	// !defined(__RRCLIENT) && !defined(__FWDSP)


/* This should be updated only once per second, by a call to update_timestamp from main thread */
// These are in main
extern char latest_timestamp[64];
extern time_t now;
static time_t last_ts_update;

// Do we need to show a timestamp in log messages?
static bool log_show_ts = false;
int log_level = LOG_CRAZY;	// Default to showing ALL logging
char latest_timestamp[64];	// Current printed timestamp

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
#if	!defined(__RRCLIENT) && !defined(__FWDSP)
   const char *ll = eeprom_get_str("debug/loglevel");
   log_show_ts = eeprom_get_bool("debug/show_ts");
#else
   const char *ll = cfg_get("log.level");
   const char *t = cfg_get("log.show-ts");
   if (t && strcasecmp(t, "true") == 0) {
      log_show_ts = true;
   }
#endif
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
      logfp = fopen(LOGFILE, "a+");

      if (!logfp) {
         Log(LOG_CRIT, "core", "Couldn't open log file at %s - %d:%s", LOGFILE, errno, strerror(errno));
         return;
      }
   }
}

void logger_end(void) {
#if	defined(HOST_POSIX)
/* This really should be HAVE_FS or such, rather than HOST_POSIX as we could log to SD, etc... */
   if (logfp) {
      fclose(logfp);
   }
#endif
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
         snprintf(latest_timestamp, sizeof(latest_timestamp), "<%ll>", time(NULL));
      }
   } else {
      return 1;
   }
   return 0;
}

char *json_escape(const char *in, char *out) {
   size_t len = strlen(in);
//   char *out = malloc(len * 6 + 1);  // worst case: every char becomes \u00XX
   char *p = out;

   for (; *in; in++) {
      switch (*in) {
         case '\"': *p++ = '\\'; *p++ = '\"'; break;
         case '\\': *p++ = '\\'; *p++ = '\\'; break;
         case '\b': *p++ = '\\'; *p++ = 'b'; break;
         case '\f': *p++ = '\\'; *p++ = 'f'; break;
         case '\n': *p++ = '\\'; *p++ = 'n'; break;
         case '\r': *p++ = '\\'; *p++ = 'r'; break;
         case '\t': *p++ = '\\'; *p++ = 't'; break;
         default:
            if ((unsigned char)*in < 0x20) {
               sprintf(p, "\\u%04x", *in);
               p += 6;
            } else {
               *p++ = *in;
            }
      }
   }

   *p = '\0';
   return out;
}

void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   char msgbuf[513];
   char ts_log_msg[1025];
   char log_msg[769];
   va_list ap;

   if (!subsys || !fmt) {
      printf("Invalid Log request: No subsys/fmt\n");
      return;
   }

   if (priority > log_level) {
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

   va_start(ap, fmt);
   /* Expand the format string */
   vsnprintf(msgbuf, 511, fmt, ap);
   memset(log_msg, 0, sizeof(log_msg));
   snprintf(log_msg, sizeof(log_msg), "<%s.%s> %s", subsys, log_priority_to_str(priority), msgbuf);

   /* Only spew to the serial port if logfile is closed */
   if (!logfp && (logfp != stdout)) {
      va_end(ap);
      return;
   }

   if (log_show_ts) {
      fprintf(logfp, "[%s] %s\n", latest_timestamp, log_msg);
      if (logfp != stdout) {
         fprintf(stdout, "x[%s] %s\n", latest_timestamp, log_msg);
      }
   } else {
      fprintf(logfp, "%s\n", log_msg);
      if (logfp != stdout) {
         fprintf(stdout, "x: %s\n", log_msg);
      }
   }
   fflush(logfp);

// if not in rrclient code, we should bcast this to users with SYSLOG flag
#if	!defined(__RRCLIENT) && !defined(__FWDSP)
   char ws_logbuf[2048];
   char ws_json_escaped[1024];
   memset(ws_json_escaped, 0, sizeof(ws_json_escaped));
   memset(ws_logbuf, 0, sizeof(ws_logbuf));
   json_escape(log_msg, ws_json_escaped);

   snprintf(ws_logbuf, sizeof(ws_logbuf), "{ \"syslog\": { \"ts\": %lu, \"subsys\": \"%s\", \"prio\": \"%s\", \"data\": \"%s\" } }",
            now, subsys, log_priority_to_str(priority), ws_json_escaped);
   struct mg_str ms = mg_str(ws_logbuf);
   ws_broadcast_with_flags(FLAG_SYSLOG, NULL, &ms, WEBSOCKET_OP_TEXT);
#endif	// !defined(__RRCLIENT)

   /* Machdep logging goes here! */

   va_end(ap);
}
