//
// gui.fb.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Virtual framebuffer, for display on LcD or web canvas
//
// We must support multiple virtual framebuffers, for things like
// the waterfall, which is rendered outside of the overall GUI
// then drawn into the main VFB or sent over the network to client
//
#include "build_config.h"
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "../ext/libmongoose/mongoose.h"
#include "rrserver/state.h"
#include "librustyaxe/logger.h"
#include "rrserver/eeprom.h"
#include "rrserver/i2c.h"
#include "rrserver/gui.h"
#include "rrserver/gui.fb.h"

gui_fb_state_t *gui_fb_init(gui_fb_state_t *fb) {
   // XXX: Get these from eeprom
   uint8_t	new_depth = 0,
                new_height = 0,
                new_width = 0;

   // If framebuffer not passed to us, allocate one to return
   if (fb == NULL) {
      if (!(fb = malloc(sizeof(gui_fb_state_t)))) {
         Log(LOG_CRIT, "fb", "out of mem. allocating fb_state");
         return NULL;
      }

      memset(fb, 0, sizeof(gui_fb_state_t));
      if (!(fb->framebuffer = malloc(new_depth * new_height * new_width))) {
         free(fb);
         return NULL;
      }
   }

   return fb;
}

// Force redrawing
bool gui_fb_update(void) {
   return false;
}

bool gui_fb_fini(gui_fb_state_t *fb) {
   if (fb) {
      free(fb);
   }
   return false;
}
