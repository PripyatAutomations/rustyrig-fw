//
// ws.audio.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include "../../ext/libmongoose/mongoose.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "rrserver/cat.h"
#include "rrserver/eeprom.h"
#include "rrserver/i2c.h"
#include "common/logger.h"
#include "common/posix.h"
#include "rrserver/state.h"
#include "rrserver/ws.h"

extern struct GlobalState rig;	// Global state

void au_send_to_ws(const void *data, size_t len) {
   struct mg_str msg = mg_str_n((const char *)data, len);
   ws_broadcast(NULL, &msg, WEBSOCKET_OP_BINARY);
}
