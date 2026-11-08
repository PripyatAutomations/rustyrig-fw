//
// rrserver/console.c: librustyaxe TUI based console
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
/*
 * Support for a console interface to the radio via io abstraction (socket|serial|pipe|ws)
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#if     defined(HOST_POSIX)
#include <sys/socket.h>
#endif
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/core.h>
#include <rrserver/console.h>
#include <rrserver/help.h>

bool cons_help(void /*io *port */) {
   return false;
}

bool cons_restart(void /*io *port */) {
   return false;
}

bool cons_shutdown(void /*io *port */) {
   return false;
}

struct cons_cmds core_cmds[] = {
//   { "config", 0, 3, cons_config },
   {
      "help", 0, 1, cons_help
   },
   {
      "restart", 0, 3, cons_restart
   },
   {
      "shutdown", 0, 3, cons_shutdown
   },
//   { "user", 0, 3, cons_user },
//   { NULL, -1, -1, NULL }
};
