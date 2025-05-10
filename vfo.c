//
// vfo.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Deal with things related to the VFO, such as reconfiguring DDS(es)
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "vfo.h"

// This should be called by CAT to set the backend appropriately
bool set_vfo_frequency(rr_vfo_type_t vfo_type, uint32_t input, float freq) {
   Log(LOG_INFO, "vfo", "Setting VFO (type: %d) input #%d to %f", vfo_type, input, freq);
   // We should call into the backend here to set the frequency
   return true;
}
