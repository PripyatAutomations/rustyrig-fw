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

// Initialize all ATU units
int atu_init(int uid) {
   int rv = 0;
   Log(LOG_INFO, "Antenna Matching Unit (ATU) #%d initialized", uid);
   return rv;
}

int atu_init_all(void) {
   int rv = 0;
   int i = 0;
   Log(LOG_INFO, "Initializing all ATUs");

   // XXX: Iterate over the available ATUs...
   rv += atu_init(i);
   return -rv;
}
