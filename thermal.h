#if	!defined(__rr_thermal_h)
#define	__rr_thermal_h
#include "config.h"

// Convert Celsius to Fahrenheit
static inline double degC_to_degF(double tempC) {
    return (tempC * 9.0 / 5.0) + 32.0;
}

// Convert Fahrenheit to Celsius
static inline double degF_to_degC(double tempF) {
    return (tempF - 32.0) * 5.0 / 9.0;
}

extern uint32_t get_thermal(uint32_t sensor);		// Query temp in degC for sensor id given
extern bool are_we_on_fire(void);		// Determine if radio is on fire and try to prevent that

#endif	// !defined(__rr_thermal_h)
