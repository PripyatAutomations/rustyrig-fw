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
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <fnmatch.h>
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

struct log_filter {
    char *pattern;            // subsystem name or wildcard, e.g. "www.*"
    logpriority_t level;      // max log level to show
    struct log_filter *next;
};

static struct log_filter *filters = NULL;

bool log_add_filter(const char *pattern, logpriority_t level) {
    struct log_filter *f = malloc(sizeof(*f));
    if (!f) {
       fprintf(stderr, "OOM in log_add_filter\n");
       return true;
    }

    f->pattern = strdup(pattern);
    if (!f->pattern) {
       free(f);
       return true;
    }
    f->level = level;
    f->next = filters;
    filters = f;
    return false;
}

void log_clear_filters(void) {
   struct log_filter *f = filters;
   while (f) {
      struct log_filter *next = f->next;
      free(f->pattern);
      free(f);
      f = next;
   }
   filters = NULL;
}

// Load filters from config string
void load_filters_from_config(void) {
   const char *cfg = cfg_get_exp("log.level");  // or "log.filters"
   if (!cfg) {
      return;
   }

   char *copy = strdup(cfg);
   free((void *)cfg);

   char *tok = copy;
   while (*tok) {
      while (isspace((unsigned char)*tok)) {
         tok++;
      }

      char *end = tok;
      while (*end && *end != ',' && !isspace((unsigned char)*end)) {
         end++;
      }

      if (*end) {
         *end = '\0';
      }

      char *sep = strchr(tok, ':');
      if (sep) {
         *sep = '\0';
         const char *subsys = tok;
         const char *level_str = sep + 1;
         logpriority_t level = log_priority_from_str(level_str);

         // If pattern ends with ".*", also add plain version
         size_t len = strlen(subsys);
         if (len >= 2 && strcmp(subsys + len - 2, ".*") == 0) {
            char *plain = strdup(subsys);
            plain[len-2] = '\0';
            log_add_filter(plain, level);
            free(plain);
         }

         log_add_filter(subsys, level);
      }

      tok = end + 1;
   }

   free(copy);
}

bool debug_filter(const char *subsys, logpriority_t msg_level) {
   struct log_filter *f = filters, *best = NULL;

   while (f) {
      if (fnmatch(f->pattern, subsys, 0) == 0) {
         if (!best || strlen(f->pattern) > strlen(best->pattern)) {
            best = f;
         }
      }
      f = f->next;
   }

   if (best) {
      // normal compare
      return msg_level <= best->level;
   }

   // default reject unless critical
   return msg_level <= LOG_CRIT;
}

// Dump all filters
void log_dump_filters(void) {
   printf("---- Log Filters ----\n");
   struct log_filter *f = filters;
   while (f) {
      printf("  '%s' = %d (%s)\n", f->pattern, f->level,
         log_priority_to_str(f->level));
      f = f->next;
   }
   printf("-------------------\n");
}

// Test whether a given subsystem + level would pass
void log_test_subsys(const char *subsys, logpriority_t level) {
   bool show = debug_filter(subsys, level);
   printf("Subsys '%s' %s level %d (%s) -> %s\n",
      subsys, (level == LOG_CRAZY) ? "CRAZY" :
               (level == LOG_DEBUG) ? "DEBUG" :
               (level == LOG_INFO) ? "INFO" :
               (level == LOG_WARN) ? "WARN" :
               (level == LOG_CRIT) ? "CRIT" :
               (level == LOG_AUDIT) ? "AUDIT" : "NONE",
      level,
      log_priority_to_str(level),
      show ? "PASS" : "DROP");
}

//////////////////////////////
// Actual log handling code //
//////////////////////////////
void logger_init(const char *logfile) {
   const char *ll = NULL;
#if defined(USE_EEPROM)
   ll = eeprom_get_str("debug/loglevel");
   log_show_ts = eeprom_get_bool("debug/show_ts");
#endif

   if (!ll) {
      ll = cfg_get("log.level");
      log_show_ts = cfg_get_bool("log.show-ts", false);
   }

   if (ll) {
      log_level = log_priority_from_str(ll);
      Log(LOG_INFO, "core", "Setting log_level to %d (%s)", log_level, ll);
   } else {
      log_level = LOG_DEBUG;
   }

   if (!logfp) {
      logfp = logfile ? fopen(logfile, "a+") : stdout;
      if (!logfp) {
         fprintf(stderr, "Couldn't open log file %s, falling back to stdout\n", logfile);
         logfp = stdout;
      }
   }

   // Load fine-grained filters from config
   load_filters_from_config();
   log_dump_filters();
}

void logger_end(void) {
   if (logfp && logfp != stdout && logfp != stderr) {
      fclose(logfp);
   }
   logfp = NULL;

   // free all registered callbacks
   struct log_callback *lp = log_callbacks;
   while (lp) {
      struct log_callback *next = lp->next;
      free(lp);
      lp = next;
   }
   log_callbacks = NULL;

   // free all filters
   log_clear_filters();
}

int update_timestamp(void) {
   struct tm *tmp;

   // If we've already updated this second or have gone back in time, return success, to save CPU
   if (last_ts_update >= now) {
      return 0;
   }

   last_ts_update = now;
   memset(latest_timestamp, 0, sizeof(latest_timestamp));

   if ((tmp = localtime(&now))) {
      /* success, proceed */
      if (strftime(latest_timestamp, sizeof(latest_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         /* handle the error */
         memset(latest_timestamp, 0, sizeof(latest_timestamp));
         snprintf(latest_timestamp, sizeof(latest_timestamp), "<%lu>", (unsigned long)now);
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

   if (!debug_filter(subsys, priority)) {
      return;
   }

   if (logfp == NULL) {
      logfp = stdout;
   }

   // this is arranged so that it will return if called more than once a second
   if (log_show_ts) {
      update_timestamp();
   }

   va_copy(ap_c1, ap);

   /* clear the message buffer */
   memset(msgbuf, 0, sizeof(msgbuf));

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
            va_list cb_ap;
            va_copy(cb_ap, ap_c1);
            lp->callback(priority, subsys, fmt, cb_ap);
            va_end(cb_ap);
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
         if (prev) {
            prev->next = i->next;
         } else {
            log_callbacks = i->next;
         }

         free(i);
         return true;  // signal success
      }
      prev = i;
      i = i->next;
   }
   return false;  // callback not found
}

bool log_add_callback(bool (*log_va_cb)(logpriority_t priority, const char *subsys, const char *fmt, va_list ap)) {
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
