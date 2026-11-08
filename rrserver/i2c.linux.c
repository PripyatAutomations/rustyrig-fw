//
// rrserver/i2c.linux.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/i2c.h>
#include <rrserver/eeprom.h>
#include <rrserver/i2c.hal.h>
#if     defined(HOST_POSIX)
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
#endif // defined(HOST_POSIX)
