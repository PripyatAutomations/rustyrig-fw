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
#include "crc.h"
#include "ant_tuner.h"
#define	ANT_TUNER
#include "atu_tables.h"

extern struct GlobalState rig;	// Global state
