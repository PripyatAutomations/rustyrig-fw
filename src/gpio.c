/*
 * Here we deal with gpio on host platform
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "config.h"
#include "state.h"
#include "parser.h"
#include "gpio.h"
#if	defined(HOST_BUILD)
#include <gpiod.h>		// Linux hosts
const int max_gpiochips = 8;
#endif

extern struct GlobalState rig;	// Global state

// Initialize platform GPIO
int gpio_init(void) {
#if	defined(HOST_BUILD)
#else
#endif
   return 0;
}
