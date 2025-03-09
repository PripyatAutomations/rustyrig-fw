//
// Here we support using the commonly available ad9959 modules which use
// an stm32 and AT commands for configuration
//
// While this is annoying, I have one of these devices, so i'd like to use it ;)
//
// Code repurposed from my ad9959-util tool
//
#include "config.h"
#include <stdio.h>
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
