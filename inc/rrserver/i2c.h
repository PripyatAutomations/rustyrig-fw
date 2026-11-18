//
// rrserver/i2c.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrserver_i2c_h)
#define __rrserver_i2c_h
#include "build_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rrserver/i2c.hal.h"

typedef struct {
   uint32_t fd;                 // File descriptor (Linux) or pointer/handle
   // (STM32)
   uint32_t bus_id;             // Bus identifier, if applicable
   uint32_t my_addr;            // My address on the bus
} I2CBus;


#endif // !defined(__rrserver_i2c_h)
