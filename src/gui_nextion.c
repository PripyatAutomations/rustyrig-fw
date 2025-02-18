//
// Here we handle displaying on Nextion HMI type displays
//
// You'll need to specify the hmi file and serial port in config!
//
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
#include "gui.h"
#include "gui_nextion.h"
