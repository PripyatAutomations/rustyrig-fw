//
// module.c: Loadable module support
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * support logging to a a few places
 *	Targets: syslog console flash (file)
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <dlfcn.h>
#include "librustyaxe/config.h"
#include "librustyaxe/logger.h"

#define	RUSTY_MODULE_API_VER	100

//
// Here we deal with loading and unloading modules
//

// Module functions
typedef struct rusty_module_hook {
   char                 *name;		// name for calling
   enum {
      CB_MSG = 0,		// MeSsaGe with length (default)
      CB_ARGV,			// argument list & count
      CB_VOID,			// no arguments
   } callback_type;
   bool                (*callback)();	// Callback
   struct rusty_module_hook	*next;			// next hook
} rusty_module_hook_t;


/////////////////////////
bool rusty_load_module(const char *path) {
   if (!path) {
      return true;
   }

   return false;
}

bool rusty_unload_module(const char *name) {
   if (!name) {
      return true;
   }

   return false;
}
