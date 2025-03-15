/*
 * Power management and monitoring
 *
 * - Try to monitor the battery and reduce TX power if it will deplete battery too soon
 * - Record usage statistics, total TX time, lifetime power used
 * - Est. lifetime and current BTU of heat production (enclosure temp - inlet temp based)
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
#include "power.h"

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
