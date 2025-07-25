// common/codecneg.c: support for using gstreamer for audio streams
// 	https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
//
// Here we handle moving audio between the server and gstreamer
//
// Here we manage negotiating codecs that are supported between both sides
// and framing audio.
//
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include "common/config.h"
#include "../../ext/libmongoose/mongoose.h"
#include "common/fwdsp-shared.h"
#include "common/logger.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "common/codecneg.h"

// if passed NULL for codecs, use all available codecs
const char *media_capab_prepare(const char *codecs) {
   const char *preferred = cfg_get("codecs.allowed");
   char *common = NULL;
   if (codecs) {
      char *common = codec_filter_common(preferred, codecs);

      if (!common) {
         return NULL;
      }
   }     

   // emit codec message
   char msgbuf[1024];
   snprintf(msgbuf, sizeof(msgbuf), "{ \"media\": { \"cmd\": \"capab\", \"payload\": \"%s\" } }", (common ? common : preferred));
   Log(LOG_DEBUG, "ws.audio", "Sending supported codec list: %s", (common ? common : preferred));

   if (common) {
      free(common);
   }

   return strdup(msgbuf);
}

char *codec_filter_common(const char *preferred, const char *available) {
   char *result = NULL;
   size_t res_sz = 0;

   if (!preferred || !available) {
      Log(LOG_CRIT, "codecneg", "codec_filter_common: preferred:<%x> available:<%x>", preferred, available);
      return NULL;
   }

   const char *p = preferred;
   while (*p) {
      while (*p == ' ') {
         p++;
      }

      if (!*p) {
         break;
      }

      const char *start = p;
      while (*p && *p != ' ') {
         p++;
      }
      size_t len = p - start;

      // Now scan `available` token-by-token
      const char *a = available;
      while (*a) {
         while (*a == ' ') {
            a++;
         }

         if (!*a) {
            break;
         }

         const char *astart = a;
         while (*a && *a != ' ') {
            a++;
         }
         size_t alen = a - astart;

         if (len == alen && memcmp(start, astart, len) == 0) {
            char *new_result = realloc(result, res_sz + len + 2);
            // we do this to make sure that we free everything
            if (!new_result) {
               if (result) {
                  free(result);
               }
               return NULL;
            }
            // Go ahead and store it
            result = new_result;
            memcpy(result + res_sz, start, len);
            res_sz += len;
            result[res_sz++] = ' ';
            result[res_sz] = 0;
            break;
         }
      }
   }

   if (res_sz > 0 && result[res_sz - 1] == ' ')
      result[--res_sz] = 0;

   return result;
}
