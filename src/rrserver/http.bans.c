//
// http.bans.c: Support for user-agent restrictions
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
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
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/posix.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/eeprom.h"
#include "rrserver/cat.h"
#include "rrserver/http.h"
#include "rrserver/ws.h"
#include "rrserver/auth.h"
#include "rrserver/ptt.h"
#include "common/util.string.h"
#include "common/util.file.h"
extern struct mg_mgr mg_mgr;

bool is_http_banned(const char *ua) {
   // Check user-agent against the the user-agent bans
   return false;
}

bool load_http_ua_bans(const char *path) {
   FILE *fp = fopen(path, "r");
   char line[1024];

   if (!fp) {
      return true;
   }

   while (!feof(fp)) {
      memset(line, 0, 1024);
      if (!fgets(line, 1024, fp)) {
         char *start = line + strspn(line, " \t\r\n");
         if (start != line) {
            memmove(line, start, strlen(start) + 1);
         }
      }
      // Skip comments and empty lines
      if (line[0] == '#' || line[0] == ';' || (strlen(line) > 1 && (line[0] == '/' && line[1] == '/')) || line[0] == '\n') {
         continue;
      }

      // Remove trailing \r or \n characters
      char *end = line + strlen(line) - 1;
      char *start = NULL;
      while (end >= line && (*end == '\r' || *end == '\n')) {
         *end = '\0';
         end--;
      }

      // Trim leading spaces (again)
      start = line + strspn(line, " \t\r\n");
      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      if (line[0] == '\n' || line[0] == '\0') {
         continue;
      }

      // If we made it this far, it's probably a valid line, parse it
   }
   fclose(fp);
   return false;
}

#endif	// defined(FEATURE_HTTP)
