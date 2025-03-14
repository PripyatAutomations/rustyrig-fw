//
// Framebuffer (LCD or similar) display backend
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
#include "gui.fb.h"

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

bool gui_fb_update(void) {
   return false;
}
