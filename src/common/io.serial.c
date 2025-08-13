//
// io.serial.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Support for serial transports for console, cat, and debugging
 */
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "common/io.h"
#include "common/logger.h"

#if	defined(HOST_POSIX)
#include <sys/socket.h>
#endif

// Here we need to abstract between the following bits:
//	CAT interpreters
//	CLI mode
//	Debug Tool
