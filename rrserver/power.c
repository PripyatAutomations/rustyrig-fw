//
// power.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Power management and monitoring
 *
 * - Try to monitor the battery and reduce TX power if it will deplete battery too soon
 * - Record usage statistics, total TX time, lifetime power used
 * - Est. lifetime and current BTU of heat production (enclosure temp - inlet temp based)
 */
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "rustyrig/state.h"
#include "rustyrig/power.h"

// 0: 12V, 1: 48V
float get_voltage(uint32_t src) {
   return 0.0;
}

float get_current(uint32_t src) {
   return 0.0;
}


float get_swr(uint32_t amp) {
   return 0.0;
}

float get_power(uint32_t amp) {
   return 0.0;
}

uint32_t check_power_thresholds(void) {
   return 0;
}
