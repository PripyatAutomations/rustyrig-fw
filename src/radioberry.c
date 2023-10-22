/*
 * Here we handle interaction with a radioberry
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
#include "cat_control.h"
#include "posix.h"

extern struct GlobalState rig;	// Global state

bool radioberry_initialized = false;

bool radioberry_init(void) {
   return false;
}
