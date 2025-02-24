#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "i2c_hal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

static int i2c_fd = -1;

int i2c_init(void) {
    i2c_fd = open("/dev/i2c-1", O_RDWR);
    return (i2c_fd < 0) ? -1 : 0;
}

int i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) return -1;
    return (write(i2c_fd, data, len) == len) ? 0 : -1;
}

int i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) return -1;
    return (read(i2c_fd, data, len) == len) ? 0 : -1;
}

void i2c_deinit(void) {
    if (i2c_fd >= 0) close(i2c_fd);
}
