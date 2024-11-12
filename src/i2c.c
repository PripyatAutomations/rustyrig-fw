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
#include "logger.h"
#include "state.h"
#include "i2c.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>
#include <stdio.h>

extern struct GlobalState rig;	// Global state
static int file_descriptor = -1;
static const char *i2c_bus = "/dev/i2c-1";

#ifdef HOST_LINUX
#include "./i2c_linux.inc.c"
#endif			// HOST_LINUX

#ifdef HOST_STM32
#include "./i2c_stm32.inc.c"
#endif 			// HOST_STM32

/*
// Initialize i2c communications
// addr: My address
int i2c_init(int bus, int addr) {
    Log(LOG_INFO, "Initializing i2c bus %d, my address is [0x%x]", bus, addr);
    return 0;
}
*/
