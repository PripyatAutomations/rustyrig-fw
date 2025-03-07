//
// Here we handle displaying on Nextion HMI type displays
//
// You'll need to specify the hmi file and serial port in config!
// See the following config keys:
//	interface/display/type: "nextion"
//	interface/display/port: serial port for the display
//	interface/display/hmi: HMI file to upload to display
//
#include "config.h"
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
#include "i2c.h"
#include "gui.h"
#include "gui_nextion.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool gui_nextion_init(void) {
   return false;
}

bool gui_nextion_update(void) {
   return false;
}
