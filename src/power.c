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

extern struct GlobalState rig;	// Global state

// 0: 12V, 1: 48V
float get_voltage(int src) {
   return 0.0;
}

float get_current(int src) {
   return 0.0;
}


float get_swr(int amp) {
   return 0.0;
}

float get_power(int amp) {
   return 0.0;
}

int check_power_thresholds(void) {
   return 0;
}
