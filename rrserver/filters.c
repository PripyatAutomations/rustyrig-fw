//
// filters.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we deal with filters, both input and output
 *
 * Build conf will generate the filter tables (filter_tables.h) in builddir
 */
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "../ext/libmongoose/mongoose.h"
#include "rustyrig/state.h"
#include "common/logger.h"
#include "rustyrig/eeprom.h"
#include "rustyrig/filters.h"
#define	FILTERS_C
#include "filter_tables.h"

int filter_init(int fid) {
    int rv = -1;
    Log(LOG_INFO, "filt", " => Filter #%d initialized", fid);
    return rv;
}

// Enumate all filters from configuration and init them
int filter_init_all(void) {
    int rv = 0;
    int i = 0;

    // Walk the configured filters and set them up
    Log(LOG_INFO, "filt", "Initializing all filters");
    filter_init(i);
    Log(LOG_INFO, "filt", "Filter setup complete");
    return rv;
}
