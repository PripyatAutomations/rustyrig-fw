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
#include "i2c_hal.h"

extern struct GlobalState rig;	// Global state
static uint32_t file_descriptor = -1;
static const char *i2c_bus = "/dev/i2c-1";

/*
// Initialize i2c communications
// addr: My address
uint32_t i2c_init(uint32_t bus, uint32_t addr) {
    Log(LOG_INFO, "Initializing i2c bus %d, my address is [0x%x]", bus, addr);
    return 0;
}
*/
