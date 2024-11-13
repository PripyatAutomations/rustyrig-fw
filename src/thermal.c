#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
#include "thermal.h"

extern struct GlobalState rig;	// Global state

// Get thermals:
// Sensor #s:
//	0	Case ambient temp
//	1	Inlet air temp
//	1000	Low-band PA Final
//	1001	Low-band PA LPF board
//	2000	High-band PA Final
//	2001	High-band PA LPF board
int get_thermal(int sensor) {
    if (sensor == 0) {
       return rig.therm_enclosure;
    } else if (sensor == 1) {
       return rig.therm_inlet;
    } else if (sensor == 1000) {
       return rig.low_amp.therm_final;
    } else if (sensor == 1001) {
       return rig.low_amp.therm_lpf;
    } else if (sensor == 2000) {
       return rig.high_amp.therm_final;
    } else if (sensor == 2001) {
       return rig.high_amp.therm_lpf;
    }

    return -1;
}

// Check the thermals and determine if we might be on fire...
bool are_we_on_fire(void) {
/*
    if (rig.therm_enclosure > thermal.encl_max) return true;
    if (rig.therm_inlet > thermal.inlet_max) return true;
    if (rig.low_amp.therm_final > thermal.final_max) return true;
    if (rig.low_amp.therm_lpf > thermal.final_lpf) return true;
    if (rig.high_amp.therm_final > thermal.final_max) return true;
    if (rig.high_amp.therm_lpf > thermal.final_lpf) return true;
*/
    return false;
}
