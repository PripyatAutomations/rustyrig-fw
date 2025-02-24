//
// Amplifier module management
//
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
#include "ptt.h"
#include "thermal.h"
#include "protection.h"
#include "amp.h"
#include "logger.h"

bool amp_init(uint8_t index) {
   Log(LOG_INFO, "Amplifier %d initialized", index);
   return true;
}
