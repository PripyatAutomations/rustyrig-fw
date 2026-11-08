//
// rrserver/i2c.hal.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__RR_I2C_HAL_H)
#define __RR_I2C_HAL_H

#include <stdint.h>
#include <stddef.h>

// Initialize the I2C bus
uint32_t i2c_init(void);

// Write to an I2C device
uint32_t i2c_write(uint8_t addr, const uint8_t *data, size_t len);

// Read from an I2C device
uint32_t i2c_read(uint8_t addr, uint8_t *data, size_t len);

// Deinitialize the I2C bus
void i2c_deinit(void);

#endif // !defined(__RR_I2C_HAL_H)
