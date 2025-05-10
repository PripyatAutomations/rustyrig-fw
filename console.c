//
// console.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Support for a console interface to the radio via io abstraction (socket|serial|pipe|ws)
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#if	defined(HOST_POSIX)
#include <sys/socket.h>
#endif
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "console.h"
#include "help.h"

bool cons_help(void /*io *port*/) {
   return false;
}

struct cons_cmds core_cmds[] = {
   { "help", 0, 1, cons_help },
//   { NULL, -1, -1, NULL }
};
