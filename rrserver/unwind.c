// rrserver/unwind.c: Support for libunwind stack unwinding
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#ifdef	USE_LIBUNWIND
#include <libunwind.h>
void print_stacktrace(void) {
   unw_cursor_t cursor;
   unw_context_t context;

   if (unw_getcontext(&context) != 0) {
      return;
   }

   if (unw_init_local(&cursor, &context) != 0) {
      return;
   }

   Log(LOG_CRIT, "core", "-------- stack dump --------");
   while (unw_step(&cursor) > 0) {
      char name[256];
      unw_word_t offset;

      if (unw_get_proc_name(&cursor, name, sizeof(name), &offset) == 0) {
         Log(LOG_CRIT, "core", "  %s (+0x%lx)", name, (long)offset);
      } else {
         Log(LOG_CRIT, "core", "  ???");
      }
   }
   Log(LOG_CRIT, "core", "-------- end stack dump --------");
}
#else	// USE_LIBUNWIND
void print_stacktrace(void) {
   Log(LOG_CRIT, "core", "**** stacktrace unavailable - we were built without libunwind ****");
}
#endif	// USE_LIBUNWIND
