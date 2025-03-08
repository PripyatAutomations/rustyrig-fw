/*
 * Here we deal with antenna matching
 *
 * Build conf will generate the LC tables used below (atu_tables.h)
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "state.h"
#include "logger.h"
#include "eeprom.h"
#include "i2c.h"
#include "atu.h"

// Tell atu_tables we want the data (we are the tuner code)
#define	ANT_TUNER
#include "atu_tables.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

// XXX: this should load the last used state for this tuner unit
atu_tv *atu_find_saved_state(int uid) {
   return NULL;
}

// Initialize all ATU units
int atu_init(int uid) {
   int rv = 0;
   atu_tv *tv = NULL;
   Log(LOG_INFO, "atu", " => ATU #%d initialized", uid);
   // do we have saved tuning parameters for this unit?
   if ((tv = atu_find_saved_state(uid)) != NULL) {
      // Apply them
   } else {
      // XXX: Should we do a DDS sweep tune, if possible?
   }

   return rv;
}

int atu_init_all(void) {
   int rv = 0;
   int tuners = 1;
//   tuners = eeprom_get_int("hw/atus");

   Log(LOG_INFO, "atu", "Initializing all ATUs (%d total)", tuners);

   // XXX: Iterate over the available ATUs and collect the return values
   for (int i = 0; i < tuners; i++) {
       if (atu_init(i)) {
          rv++;
       }
   }

   Log(LOG_INFO, "atu", "ATU setup complete");
   return -rv;
}
