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
#include <stdio.h>
#include <gpiod.h>		// Linux hosts
#define	MAX_GPIOCHIPS		8
radio_gpiochip gpiochips[MAX_GPIOCHIPS];
#endif

extern struct GlobalState rig;	// Global state

// right now we only support one gpio chip, but this wrapper should ease transition
uint32_t radio_find_gpiochip(const char *name) {
// On posix hosts, such as linux on pi, we use libgpiod to access gpio, add other platforms here
#if	defined(HOST_POSIX)
   for (uint32_t i = 0; i < MAX_GPIOCHIPS; i++) {
      if (strcasecmp(gpiochips[i].key, name) == 0) {
         return i;
      }
   }
   return -1;
#endif
}

uint32_t radio_gpiochip_init(const char *chipname) {
   uint32_t i = -1;

   // Does it already exist?
   if ((i = radio_find_gpiochip(chipname)) != -1) {
      Log(LOG_WARN, "gpio chip %s is already initialized at index %d", i);
      return -1;
   }

// On posix hosts, such as linux on pi, we use libgpiod to access gpio, add other platforms here
#if	defined(HOST_POSIX)
   struct gpiod_chip *tmp = NULL;

   if ((tmp = gpiod_chip_open(chipname)) == NULL) {
// XXX: v1 api remnant, safe to remove?
//   if ((tmp = gpiod_chip_open_by_name(chipname)) == NULL) {
      Log(LOG_CRIT, "error opening gpio chip %s", chipname);
      return -1;
   }

   bool slot_found = false;
   for (i = 0; i < MAX_GPIOCHIPS; i++) {
      // find the first empty slot
      if (!gpiochips[i].active && gpiochips[i].chip == NULL) {
         slot_found = true;
         break;
      }
   }
#endif

   if (slot_found) {
      Log(LOG_INFO, "Initializing GPIO chip %s at index %i [ptr: %x]", chipname, i, tmp);
      gpiochips[i].chip = tmp;
      snprintf(gpiochips[i].key, GPIO_KEYLEN, "%s", chipname);
      gpiochips[i].active = true;
      return i;
   } else {
      Log(LOG_CRIT, "No free GPIO chip slots (max: %d, used: %d)", MAX_GPIOCHIPS, i);
      return -1;
   }
}
   
// Initialize platform GPIO
uint32_t gpio_init(void) {
#if	defined(HOST_POSIX)
#endif
   Log(LOG_INFO, "Initializing gpio");
   radio_gpiochip_init("main");
   return 0;
}
