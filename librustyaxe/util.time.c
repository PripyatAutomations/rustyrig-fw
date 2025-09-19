//
// util.string.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/core.h>
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
#include <errno.h>

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

time_t dhms2time_t(const char *str) {
   time_t seconds = 0;
   char *copy = NULL;

   if (str == NULL) {
      fprintf(stderr, "+ERROR timestr2time_t: passed NULL str\n");
      return 0;
   }
   
   size_t len = strlen(str);
   if ((copy = malloc(len + 1)) == NULL) {
      fprintf(stderr, "+ERROR timestr2time_t: out of memory\n");
      exit(ENOMEM);
   }

   memset(copy, 0, len + 1);
   memcpy(copy, str, len);

   char *multipliers = "ywdhms";
   char *ptr = copy;

   while (*ptr != '\0') {
      // Find the numeric value and extract the unit character
      int value = strtol(ptr, &ptr, 10);
      char unit = *ptr;

      switch (unit) {
         case 'y':
            seconds += value * 365 * 24 * 60 * 60;  // Convert years to seconds (assuming 365 days per year)
            break;
         case 'w':
            seconds += value * 7 * 24 * 60 * 60;  // Convert weeks to seconds
            break;
         case 'd':
            seconds += value * 24 * 60 * 60;  // Convert days to seconds
            break;
         case 'h':
            seconds += value * 60 * 60;  // Convert hours to seconds
            break;
         case 'm':
            seconds += value * 60;  // Convert minutes to seconds
            break;
         case 's':
            seconds += value;  // Add seconds
            break;
      }

      ptr++;  // Move to the next character
   }

   free(copy);  // Free the memory allocated for the copy
   return seconds;
}
