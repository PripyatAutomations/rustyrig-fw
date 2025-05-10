//
// i2c.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we deal with i2c messages, most importantly logging them
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>
#include <stdio.h>
#include "logger.h"
#include "state.h"
#include "i2c.h"
#include "i2c.hal.h"

static uint32_t file_descriptor = -1;
static const char *i2c_bus = "/dev/i2c-1";

/*
// Initialize i2c communications
// addr: My address
uint32_t i2c_init(uint32_t bus, uint32_t addr) {
    Log(LOG_INFO, "i2c", "Initializing i2c bus %d, my address is [0x%x]", bus, addr);
    return 0;
}
*/
