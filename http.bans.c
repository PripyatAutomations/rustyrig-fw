//
// http.bans.c: Support for user-agent restrictions
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
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
#include "inc/i2c.h"
#include "inc/state.h"
#include "inc/eeprom.h"
#include "inc/logger.h"
#include "inc/cat.h"
#include "inc/posix.h"
#include "inc/http.h"
#include "inc/ws.h"
#include "inc/auth.h"
#include "inc/ptt.h"
#include "inc/util.string.h"
#include "inc/util.file.h"
extern struct mg_mgr mg_mgr;

bool is_http_banned(const char *ua) {
   // Check user-agent against the the user-agent bans
   return false;
}

bool load_http_ua_bans(const char *path) {
   return false;
}

#endif	// defined(FEATURE_HTTP)
