//
// Here we support plotting a waterfall to either display on nextion or send via network
//

#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "eeprom.h"
#include "logger.h"
#include "gui.h"
#include "waterfall.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state
