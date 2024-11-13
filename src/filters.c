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

extern struct GlobalState rig;	// Global state
