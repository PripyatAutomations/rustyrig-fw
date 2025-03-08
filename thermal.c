//
// Thermal monitoring and shutdown feature
//
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
#include "logger.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

typedef struct ThermalLimits {
  int32_t encl_warn;
  int32_t encl_max;
  int32_t encl_target;
  int32_t final_warn;
  int32_t final_max;
  int32_t final_target;
  int32_t inlet_warn;
  int32_t inlet_max;
  int32_t inlet_target;
  int32_t lpf_warn;
  int32_t lpf_max;
  int32_t lpf_target;
} ThermalLimits;

// Set some sane default values in case the user didn't bother to configure them...
struct ThermalLimits thermal_limits = {
   .encl_max = 140,
   .encl_warn = 130,
   .final_max = 190,
   .final_warn = 170,
   .inlet_max = 120,
   .inlet_warn = 100,
   .lpf_max = 120,
   .lpf_warn = 100
};

// Get thermals:
// Sensor #s:
//	0	Case ambient temp
//	1	Inlet air temp
//	1000	Low-band PA Final
//	1001	Low-band PA LPF board
//	2000	High-band PA Final
//	2001	High-band PA LPF board
uint32_t get_thermal(uint32_t sensor) {
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
    // Send an alarm to log, but return TRUE for warnings
    if (rig.therm_enclosure >= thermal_limits.encl_warn)
       Log(LOG_CRIT, "therm", "THERMAL WARNING: Enclosure %d > %d degF!", rig.therm_enclosure, thermal_limits.encl_warn);
    if (rig.therm_inlet >= thermal_limits.inlet_warn)
       Log(LOG_CRIT, "therm", "THERMAL WARNING: Inlet %d > %d degF!", rig.therm_enclosure, thermal_limits.encl_warn);
    if (rig.low_amp.therm_lpf >= thermal_limits.lpf_warn)
       Log(LOG_CRIT, "therm", "THERMAL WARNING: LPF (Low) %d > %d degF!", rig.low_amp.therm_lpf, thermal_limits.lpf_warn);
    if (rig.high_amp.therm_lpf >= thermal_limits.lpf_warn)
       Log(LOG_CRIT, "therm", "THERMAL WARNING: LPF (High) %d > %d degF!", rig.high_amp.therm_lpf, thermal_limits.lpf_warn);
    if (rig.low_amp.therm_final >= thermal_limits.lpf_warn)
       Log(LOG_CRIT, "therm", "THERMAL WARNING: FINALS (Low) %d > %d degF!", rig.low_amp.therm_final, thermal_limits.final_warn);
    if (rig.high_amp.therm_final >= thermal_limits.lpf_warn)
       Log(LOG_CRIT, "therm", "THERMAL WARNING: FINALS (High) %d > %d degF!", rig.high_amp.therm_final, thermal_limits.final_warn);

    // Return TRUE on any max at or exceeded
    if (rig.therm_enclosure >= thermal_limits.encl_max) return true;
    if (rig.therm_inlet >= thermal_limits.inlet_max) return true;
    if (rig.low_amp.therm_final >= thermal_limits.final_max) return true;
    if (rig.low_amp.therm_lpf >= thermal_limits.lpf_max) return true;
    if (rig.high_amp.therm_final >= thermal_limits.final_max) return true;
    if (rig.high_amp.therm_lpf >= thermal_limits.lpf_max) return true;
    return false;
}
