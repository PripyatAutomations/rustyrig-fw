// A wrapper for the io subsystem to use mongoose on supported platforms
///
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "mongoose.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

