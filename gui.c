//
// Here we deal with common GUI operations between HMI and framebuffer
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

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool gui_init(void) {
   return false;
}

bool gui_update(void) {
   // Refresh Nextion display if present
   if (gui_nextion_update()) {
      return true;
   }

   // Refresh the framebuffer (for LED and HTTP)
   if (gui_fb_update()) {
      return true;
   }

   return false;
}
