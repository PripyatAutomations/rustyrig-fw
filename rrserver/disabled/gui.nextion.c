//
// gui.nextion.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
// Here we handle displaying on Nextion HMI type displays
//
// You'll need to specify the hmi file and serial port in config!
// See the following config keys:
// interface/display/type: "nextion"
// interface/display/port: serial port for the display
// interface/display/hmi: HMI file to upload to display
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
#include <rrserver/gui.nextion.h>

bool gui_nextion_init(void) {
   return false;
}

bool gui_nextion_update(void) {
   return false;
}
