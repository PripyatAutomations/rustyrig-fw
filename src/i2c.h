#if	!defined(_i2c_h)
#define	_i2c_h
#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "i2c_hal.h"

typedef struct {
    uint32_t fd; 		// File descriptor (Linux) or pointer/handle (STM32)
    uint32_t bus_id;		// Bus identifier, if applicable
    uint32_t my_addr; 	// My address on the bus
} I2CBus;


#endif	// !defined(_i2c_h)
