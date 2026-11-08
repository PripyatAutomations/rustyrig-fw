//
// gui.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
// Here we deal with common GUI operations between HMI and framebuffer
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/i2c.h>
#include <rrserver/gui.h>

bool gui_init(void) {
   // Initialize the TUI
   // Initialize the GTK stuff if configured
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
