//
// i2c.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_i2c_h)
#define	__rr_i2c_h
#include "rrserver/config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rrserver/i2c.hal.h"

typedef struct {
    uint32_t fd; 		// File descriptor (Linux) or pointer/handle (STM32)
    uint32_t bus_id;		// Bus identifier, if applicable
    uint32_t my_addr; 	// My address on the bus
} I2CBus;


#endif	// !defined(__rr_i2c_h)
