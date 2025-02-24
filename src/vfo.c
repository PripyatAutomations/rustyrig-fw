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

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool set_vfo_frequency(VFOType vfo_type, uint32_t input, float freq) {
   Log(LOG_INFO, "Setting VFO (type: %d) input #%d to %f", vfo_type, input, freq);
   return true;
}
