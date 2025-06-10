//
// http.bans.c: Support for user-agent restrictions
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "rustyrig/config.h"
#if	defined(FEATURE_HTTP)
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include "rustyrig/i2c.h"
#include "rustyrig/state.h"
#include "rustyrig/eeprom.h"
#include "rustyrig/logger.h"
#include "rustyrig/cat.h"
#include "rustyrig/posix.h"
#include "rustyrig/http.h"
#include "rustyrig/ws.h"
#include "rustyrig/auth.h"
#include "rustyrig/ptt.h"
#include "rustyrig/util.string.h"
#include "rustyrig/util.file.h"
extern struct mg_mgr mg_mgr;

bool is_http_banned(const char *ua) {
   // Check user-agent against the the user-agent bans
   return false;
}

bool load_http_ua_bans(const char *path) {
   return false;
}

#endif	// defined(FEATURE_HTTP)
