/*
 * Support for a console interface to the radio via io abstraction (socket|serial|pipe)
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

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool cons_help(void /*io *port*/) {
   return false;
}

struct cons_cmds core_cmds[] = {
   { "help", 0, 1, cons_help },
//   { NULL, -1, -1, NULL }
};
