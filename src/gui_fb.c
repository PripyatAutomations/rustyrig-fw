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
#include "gui_fb.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool gui_fb_init(void) {
   return false;
}

bool gui_fb_update(void) {
   return false;
}
