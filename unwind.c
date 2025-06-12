/*
 * If enabled, use linunwind so can dump stack traces
 */
#include <stdio.h>
#include "build_config.h"

#if	defined(USE_LIBUNWIND)
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

   while (unw_step(&cursor) > 0) {
      char name[256];
      unw_word_t offset;
      if (unw_get_proc_name(&cursor, name, sizeof(name), &offset) == 0) {
         fprintf(stderr, "  %s (+0x%lx)\n", name, (long)offset);
      } else {
         fprintf(stderr, "  ???\n");
      }
   }
}
#else
void print_stacktrace(void) {
   printf("\n\n**** stacktrace unavailable - we were built without libunwind ****\n\n");
}
#endif
