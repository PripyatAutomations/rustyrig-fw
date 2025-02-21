#if	!defined(_console_h)
#define	_console_h

#include "config.h"
#include <stdbool.h>

struct cons_cmds {
   char verb[16];
   int min_args,
       max_args;
   bool (*hndlr)();
};
#endif	// !defined(_console_h)
