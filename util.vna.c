/*
 * util_vna.c: Lightweight Vector Network Analyzer features
 * 
 * Here we support using the AD9866 to sweep the antenna and
 * provide a data set for calculating L&C values in the ant_tuner
 * code.
 *
 * Someone implement this ;)
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "util.vna.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state
