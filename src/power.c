#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "config.h"
#include "state.h"
#include "power.h"

extern struct GlobalState *rig;	// Global state

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
