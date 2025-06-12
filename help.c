//
// help.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we support a help system, if filesystem is present
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "ext/libmongoose/mongoose.h"
#include "rustyrig/i2c.h"
#include "rustyrig/state.h"
#include "rustyrig/eeprom.h"
#include "common/logger.h"
#include "rustyrig/cat.h"
#include "common/posix.h"
#include "rustyrig/io.h"

bool send_help(rr_io_context_t *port, const char *topic) {
   return false;
}
