/*
 * Support for serial transports for console, cat, and debugging
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"

#if	defined(HOST_POSIX)
#include <sys/socket.h>
#endif

extern struct GlobalState rig;	// Global state

// Here we need to abstract between the following bits:
//	CAT interpreters
//	CLI mode
//	Debug Tool
