//
// debug.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we support debugging features of various sorts
//
// Mostly we extend the Logger feature to handle filtering
// of the debug messages.
//
// See the 'help debug' command in console to set this up

#include "librustyaxe/config.h"
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "librustyaxe/logger.h"
//#include "rrserver/eeprom.h"

bool debug_init(void) {
   // Nothing to do yet
   return false;
}

bool debug_filter(const char *subsys, const char *fmt) {
   // XXX: Figure out if the debug level is desired by the user and if so return true
   return true;

   // Nope, discard the message
//   return false;
}
