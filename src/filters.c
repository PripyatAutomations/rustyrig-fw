/*
 * Here we deal with filters, both input and output
 *
 * Build conf will generate the filter tables (filter_tables.h) in builddir
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
#include "filters.h"
#define	FILTERS_C
#include "filter_tables.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

int filter_init(int fid) {
    int rv = -1;
    Log(LOG_INFO, " => Filter #%d initialized", fid);
    return rv;
}

// Enumate all filters from configuration and init them
int filter_init_all(void) {
    int rv = 0;
    int i = 0;

    // Walk the configured filters and set them up
    Log(LOG_INFO, "Initializing all filters");
    filter_init(i);
    Log(LOG_INFO, "Filter setup complete");
    return rv;
}
