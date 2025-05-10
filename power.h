//
// power.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_power_h)
#define	__rr_power_h

extern float get_voltage(uint32_t src);
extern float get_current(uint32_t src);
extern float get_swr(uint32_t amp);
extern float get_power(uint32_t amp);
extern uint32_t check_power_thresholds(void);

#endif	// !defined(__rr_power_h)
