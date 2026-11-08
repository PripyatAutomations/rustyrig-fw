//
// rrserver/console.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrserver_console_h)
#define __rrserver_console_h

#include "build_config.h"
#include <stdbool.h>

struct cons_cmds {
   char verb[16];
   int min_args, max_args;
   bool (*hndlr)();
};
#endif // !defined(__rrserver_console_h)
