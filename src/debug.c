//
// Here we support debugging features of various sorts
//
// Mostly we extend the Logger feature to handle filtering
// of the debug messages.
//
// See the 'help debug' command in console to set this up

#include "config.h"
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "state.h"
#include "logger.h"
#include "eeprom.h"
#include "debug.h"

bool debug_init(void) {
   // Nothing to do yet
   return false;
}
