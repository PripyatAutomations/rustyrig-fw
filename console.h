#if	!defined(__rr_console_h)
#define	__rr_console_h

#include "config.h"
#include <stdbool.h>

struct cons_cmds {
   char verb[16];
   int min_args,
       max_args;
   bool (*hndlr)();
};
#endif	// !defined(__rr_console_h)
