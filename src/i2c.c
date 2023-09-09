/*
 * Here we deal with i2c messages, most importantly logging them
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "logger.h"
#include "state.h"
extern struct GlobalState rig;	// Global state

// Initialize i2c communications
// addr: My address
int i2c_init(int bus, int addr) {
    Log(INFO, "Initializing i2c bus %d, my address is [0x%x]", bus, addr);
    return 0;
}
