//
// ws.audio.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "inc/cat.h"
#include "inc/codec.h"
#include "inc/eeprom.h"
#include "inc/i2c.h"
#include "inc/logger.h"
#include "inc/posix.h"
#include "inc/state.h"
#include "inc/ws.h"

extern struct GlobalState rig;	// Global state

void au_send_to_ws(const void *data, size_t len) {
   struct mg_str msg = mg_str_n((const char *)data, len);
   ws_broadcast(NULL, &msg);
}
