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
#include "logger.h"

#if	defined(HOST_POSIX)
#include <gpiod.h>		// Linux hosts
const int max_gpiochips = 8;
#endif

extern struct GlobalState rig;	// Global state

// right now we only support one gpio chip, but this wrapper should ease transition
struct gpiod_chip *radio_find_gpiochip(const char *name) {
//   return globals.gpiochip;
   return 0;
}

int radio_gpiochip_init(const char *chipname) {
   int chip = -1;

   if (radio_find_gpiochip(chipname) != NULL) {
      Log(WARN, "gpio chip %s is already initialized");
      return chip;
   }

#if	defined(HOST_POSIX)
//   chip = gpiod_chip_open_by_name(chipname);
#endif
   return chip;
}
   
// Initialize platform GPIO
int gpio_init(void) {
#if	defined(HOST_POSIX)
#endif
   return 0;
}
