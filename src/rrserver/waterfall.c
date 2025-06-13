// waterfall.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we support plotting a waterfall onto virtual fb
// to be sent either to display or network client
//
#include "build_config.h"
#include "common/config.h"
#include "../ext/libmongoose/mongoose.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "rustyrig/eeprom.h"
#include "common/logger.h"
#include "rustyrig/gui.h"
#include "rustyrig/waterfall.h"
