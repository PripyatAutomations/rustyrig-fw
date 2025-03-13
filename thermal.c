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
//	10xx	Final on PA xx
//	20xx	ATU xx
//	30xx	Filter xx
uint32_t get_thermal(uint32_t sensor) {
    if (sensor == 0) {
       return rig.therm_enclosure;
    } else if (sensor == 1) {
       return rig.therm_inlet;
    } else if (sensor >= 1000 && sensor <= 1099) {
      int unit = sensor - 1000;
      if (unit > RR_MAX_AMPS || unit < 0) {
         Log(LOG_DEBUG, "therm", "Request for invalid sensor %d (max: %d) returning -1000!", sensor, RR_MAX_AMPS);
         return -1000;
      } else {
         float temp = rig.amps[unit].thermal;
         Log(LOG_DEBUG, "therm.noisy", "returning thermal %d (PA: %d): %f degF", sensor, unit, temp);
         return temp;
      }
    } else if (sensor >= 2000 && sensor <= 2099) {
      int unit = sensor - 2000;
      if (unit > RR_MAX_ATUS || unit < 0) {
         Log(LOG_DEBUG, "therm", "Request for invalid sensor %d (max: %d) returning -1000!", sensor, RR_MAX_ATUS);
         return -1000;
      } else {
         float temp = rig.atus[unit].thermal;
         Log(LOG_DEBUG, "therm.noisy", "returning thermal %d (ATU: %d): %f degF", sensor, unit, temp);
         return temp;
      }
    } else if (sensor >= 3000 && sensor <= 3099) {
      int unit = sensor - 3000;
      if (unit > RR_MAX_FILTERS || unit < 0) {
         Log(LOG_DEBUG, "therm", "Request for invalid sensor %d (max: %d) returning -1000!", sensor, RR_MAX_FILTERS);
         return -1000;
      } else {
         float temp = rig.filters[unit].thermal;
         Log(LOG_DEBUG, "therm.noisy", "returning thermal %d (Filter: %d): %f degF", sensor, unit, temp);
         return temp;
      }
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

    // Check each installed module
    for (int i = 0; i < RR_MAX_AMPS; i++) {
        if (rig.amps[i].thermal >= thermal_limits.final_max) {
           Log(LOG_CRIT, "therm", "THERMAL ALARM: AMP %d %d > %d degF!", i, rig.amps[i].thermal, thermal_limits.final_max);
           return true;
        }
        if (rig.amps[i].thermal >= thermal_limits.final_warn) {
           Log(LOG_CRIT, "therm", "THERMAL WARNING: ANP %d %d > %d degF!", i, rig.amps[i].thermal, thermal_limits.final_warn);
        }
    }

    // Return TRUE on any max at or exceeded
    if (rig.therm_enclosure >= thermal_limits.encl_max) {
       Log(LOG_CRIT, "therm", "THERMAL ALARM: Case %d > %d degF!", rig.therm_enclosure, thermal_limits.encl_max);
       return true;
    }

    if (rig.therm_inlet >= thermal_limits.inlet_max) {
       Log(LOG_CRIT, "therm", "THERMAL ALARM: Inlet %d > %d degF!", rig.therm_inlet, thermal_limits.inlet_max);
       return true;
    }
    return false;
}
