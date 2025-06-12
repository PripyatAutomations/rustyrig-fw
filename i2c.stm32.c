//
// i2c.stm32.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "ext/libmongoose/mongoose.h"
#include "rustyrig/i2c.h"
#include "rustyrig/state.h"
#include "rustyrig/eeprom.h"
#include "common/logger.h"
#include "rustyrig/cat.h"
#include "common/posix.h"
#include "rustyrig/i2c_hal.h"
#include "rustyrig/stm32f1xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

int i2c_init(void) {
    return 0; // Assume hi2c1 is initialized in main.c
}

int i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    return (HAL_I2C_Master_Transmit(&hi2c1, addr << 1, (uint8_t *)data, len, HAL_MAX_DELAY) == HAL_OK) ? 0 : -1;
}

int i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    return (HAL_I2C_Master_Receive(&hi2c1, addr << 1, data, len, HAL_MAX_DELAY) == HAL_OK) ? 0 : -1;
}

void i2c_deinit(void) {
    HAL_I2C_DeInit(&hi2c1);
}
