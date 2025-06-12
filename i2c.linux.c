//
// i2c.linux.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#if	defined(HOST_POSIX)
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
#include "rustyrig/i2c.hal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

static int i2c_fd = -1;

uint32_t i2c_init(void) {
    i2c_fd = open("/dev/i2c-1", O_RDWR);
    return (i2c_fd < 0) ? -1 : 0;
}

uint32_t i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) {
       return -1;
    }
    return (write(i2c_fd, data, len) == len) ? 0 : -1;
}

uint32_t i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) {
       return -1;
    }
    return (read(i2c_fd, data, len) == len) ? 0 : -1;
}

void i2c_deinit(void) {
    if (i2c_fd >= 0) {
       close(i2c_fd);
    }
}
#endif	// defined(HOST_POSIX)
