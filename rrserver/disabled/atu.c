//
// atu.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we deal with antenna matching
 *
 * Build conf will generate the LC tables used below (atu_tables.h)
 */
#include "build_config.h"
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include <rrserver/eeprom.h>
#include <rrserver/i2c.h>
#include <rrserver/atu.h>

// Tell atu_tables we want the data (we are the tuner code)
#define	ANT_TUNER
//#include "atu_tables.h"

// Extract the memories from eeprom and optionally json file on posix
bool rr_atu_load_memories(int unit) {
   if (unit < 0) {
      return true;
   }

#if	defined(HOST_POSIX)
   // Open the json configuration file, if present
#endif
   // Look up ATU memory header
   int active_slots = 0;

   // Get the eeprom offset of the start of ATU mem slots
   // Look up ATU channels defined by the header
   for (int i = 0; i < active_slots; i++) {
      // Apply offset to base_addr
   }
   return false;
}

static rr_atu_tv *tv_is_closest(rr_atu_tv *low, rr_atu_tv *high) {
   // shortcut for invalid calls
   if (!low && !high) {
      return NULL;
   }

   // If only high value provided, return it
   if (!low && high) {
      return high;
   }

   // if only low value provided, return it
   if (!high && low) {
      return low;
   }

   // fallthru case
   return NULL;
}

// XXX: this should load the last used state for this tuner unit
rr_atu_tv *rr_atu_find_saved_state(int uid) {
   if (uid < 0) {
      return NULL;
   }

   rr_atu_tv *closest_low = NULL,
             *closest_high = NULL;

//   if (tv_is_closest(closest_low, closest_high)) {
//   }

   // If we make it here, no close values were found
   return NULL;
}

// Initialize all ATU units
int rr_atu_init(int uid) {
   if (uid < 0) {
     return -1;
   }

   int rv = 0;
   rr_atu_tv *tv = NULL;
   Log(LOG_INFO, "atu", " => ATU #%d initializing", uid);
   // do we have saved tuning parameters for this unit?
   if ((tv = rr_atu_find_saved_state(uid))) {
      // Apply them
   }

   return rv;
}

int rr_atu_init_all(void) {
   int rv = 0;
   int tuners = 1;
//   tuners = eeprom_get_int("hw/atus");

   Log(LOG_INFO, "atu", "Initializing all ATUs (%d total)", tuners);

   // XXX: Iterate over the available ATUs and collect the return values
   for (int i = 0; i < tuners; i++) {
       if (rr_atu_init(i)) {
          rv++;
       }
   }

   Log(LOG_INFO, "atu", "ATU setup complete with %d warning/issues", rv);
   return -rv;
}
