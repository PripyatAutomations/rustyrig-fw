//
// dds.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we managed attached Direct Digital Synthesizer devices
//
#include "build_config.h"
#include "common/config.h"
#include <stdio.h>
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
#include "common/logger.h"
#include "rrserver/eeprom.h"
#include "rrserver/i2c.h"

bool dds_init(void) {
   return false;
}

