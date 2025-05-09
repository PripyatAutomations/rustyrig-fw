#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "cat.h"
#include "codec.h"
#include "eeprom.h"
#include "i2c.h"
#include "logger.h"
#include "posix.h"
#include "state.h"
#include "ws.h"

extern struct GlobalState rig;	// Global state

void au_send_to_ws(const void *data, size_t len) {
   struct mg_str msg = mg_str_n((const char *)data, len);
   ws_broadcast(NULL, &msg);
}
