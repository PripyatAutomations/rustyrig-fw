//
// rrserver/audit.c: Store LOG_AUDIT level Log() messages in the master database
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrserver/database.h>

#ifdef USE_SQLITE
// We register a callback with the logger (see librustyaxe/logger.c), so every
// Log(LOG_AUDIT, ...) call from anywhere (librrprotocol, rrserver, etc) is
// written to the audit_log table via db_add_audit_event() - see database.c
//
// The logger doesn't know which user (if any) triggered a message, so we
// record '-' as the username and use the log subsystem as the event_type.
// Callers wanting richer audit records (real usernames, cptr, etc) can call
// db_add_audit_event() directly.
//

// Called by Log() for every message; the return value is ignored by the logger
static bool audit_log_cb(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   if (priority != LOG_AUDIT || !subsys || !fmt) {
      return false;
   }

   // No db (yet)? Nothing to do, the message still goes to logfile/console
   if (!masterdb) {
      return false;
   }

   char details[1024];
   memset( details, 0, sizeof(details) );
   vsnprintf(details, sizeof(details), fmt, ap);

   if (!db_add_audit_event(masterdb, "-", subsys, details) ) {
      // Do NOT Log() at LOG_AUDIT level in here or we might recurse!
      Log(LOG_WARN, "audit.db", "Failed to save audit event, type:<%s>", subsys);
   }

   return false;
}

// Register our callback with the logger; called by main() after db_open()
void audit_init(void) {
   log_add_callback(audit_log_cb);
   Log(LOG_DEBUG, "audit", "Storing LOG_AUDIT level messages in the database");
}
#endif	// USE_SQLITE
