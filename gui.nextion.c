//
// gui.nextion.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we handle displaying on Nextion HMI type displays
//
// You'll need to specify the hmi file and serial port in config!
// See the following config keys:
//	interface/display/type: "nextion"
//	interface/display/port: serial port for the display
//	interface/display/hmi: HMI file to upload to display
//
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "inc/state.h"
#include "inc/logger.h"
#include "inc/eeprom.h"
#include "inc/i2c.h"
#include "inc/gui.h"
#include "inc/gui.nextion.h"

bool gui_nextion_init(void) {
   return false;
}

bool gui_nextion_update(void) {
   return false;
}
