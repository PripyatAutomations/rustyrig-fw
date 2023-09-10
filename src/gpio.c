/*
 * Here we deal with gpio on our various platforms
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
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
