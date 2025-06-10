//
// thermal.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_thermal_h)
#define	__rr_thermal_h
#include "rustyrig/config.h"

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

// Convert Celsius to Fahrenheit
static inline double degC_to_degF(double tempC) {
    return (tempC * 9.0 / 5.0) + 32.0;
}

// Convert Fahrenheit to Celsius
static inline double degF_to_degC(double tempF) {
    return (tempF - 32.0) * 5.0 / 9.0;
}

extern struct ThermalLimits thermal_limits;
extern uint32_t get_thermal(uint32_t sensor);		// Query temp in degC for sensor id given
extern bool are_we_on_fire(void);		// Determine if radio is on fire and try to prevent that

#endif	// !defined(__rr_thermal_h)
