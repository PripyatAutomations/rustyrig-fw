//
// gpio.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we deal with gpio on our various platforms
 */
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "inc/state.h"
#include "inc/gpio.h"
#include "inc/logger.h"

#if	defined(HOST_POSIX)
#include <stdio.h>
#include <gpiod.h>		// Linux hosts
#define	MAX_GPIOCHIPS		8
radio_gpiochip gpiochips[MAX_GPIOCHIPS];
#endif

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
      Log(LOG_WARN, "gpio", "gpio chip %s is already initialized at index %d", i);
      return -1;
   }

// On posix hosts, such as linux on pi, we use libgpiod to access gpio, add other platforms here
#if	defined(HOST_POSIX)
   struct gpiod_chip *tmp = NULL;

   if ((tmp = gpiod_chip_open(chipname)) == NULL) {
// XXX: v1 api remnant, safe to remove?
//   if ((tmp = gpiod_chip_open_by_name(chipname)) == NULL) {
      Log(LOG_CRIT, "gpio", "error opening gpio chip %s", chipname);
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
      Log(LOG_INFO, "gpio", "Initializing GPIO chip %s at index %i [ptr: %x]", chipname, i, tmp);
      gpiochips[i].chip = tmp;
      snprintf(gpiochips[i].key, GPIO_KEYLEN, "%s", chipname);
      gpiochips[i].active = true;
      return i;
   } else {
      Log(LOG_CRIT, "gpio", "No free GPIO chip slots (max: %d, used: %d)", MAX_GPIOCHIPS, i);
      return -1;
   }
}

// Initialize platform GPIO
uint32_t gpio_init(void) {
   Log(LOG_INFO, "gpio", "Initializing gpio");

   // XXX: Walk over configured list of gpio interfaces from config
   radio_gpiochip_init("main");
   return 0;
}
