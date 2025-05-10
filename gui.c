//
// gui.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
