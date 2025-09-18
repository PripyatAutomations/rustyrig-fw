//
// util.string.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/config.h>
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
#include <time.h>
#include <librustyaxe/util.string.h>
#include <librustyaxe/core.h>

extern time_t now;

////////////////////
// Chat timestamp //
////////////////////
static char chat_ts[32];
static time_t chat_ts_updated = 0;

const char *get_chat_ts(time_t ts) {
   time_t now = time(NULL);

   // If this is "live now", allow caching per second
   if (ts == 0 || ts >= now) {
      if (chat_ts_updated == now) {
         return chat_ts;   // use cached string
      }
      chat_ts_updated = now;
      ts = now;
   }

   struct tm tmsg, tcurr;
   localtime_r(&ts, &tmsg);
   localtime_r(&now, &tcurr);

   // Check if the message is from today
   if (tmsg.tm_year == tcurr.tm_year &&
       tmsg.tm_yday == tcurr.tm_yday) {
      strftime(chat_ts, sizeof(chat_ts), "%H:%M:%S", &tmsg);
   } else {
      strftime(chat_ts, sizeof(chat_ts), "%a %b %d %H:%M:%S", &tmsg);
   }

   return chat_ts;
}
