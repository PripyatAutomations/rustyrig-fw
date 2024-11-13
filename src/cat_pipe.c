#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "vfo.h"
extern struct GlobalState rig;	// Global state

// here we provide access to the CAT protocols over a unix pipe instead of a real serial port
#if	defined(HOST_POSIX)
#include <stdio.h>
#include <fcntl.h>



#endif		// defined(HOST_POSIX)
