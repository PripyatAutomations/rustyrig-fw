// librustyaxe/codecneg.c: support for using gstreamer for audio streams
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
#include <librustyaxe/config.h>
#include <librustyaxe/fwdsp-shared.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/posix.h>
#include <librustyaxe/util.file.h>
#include <librustyaxe/codecneg.h>

// if passed NULL for codecs, use all available codecs
const char *media_capab_prepare(const char *codecs) {
   if (!codecs) {
      return NULL;
   }

   // emit codec message
   char msgbuf[1024];
   snprintf(msgbuf, sizeof(msgbuf), "{ \"media\": { \"cmd\": \"capab\", \"codecs\": \"%s\" } }", codecs);

   return strdup(msgbuf);
}

char *codec_filter_common(const char *preferred, const char *available) {
   char *result = NULL;
   size_t res_sz = 0;

   if (!preferred || !available) {
      Log(LOG_WARN, "codecneg", "codec_filter_common: empty list -- preferred:<%x> available:<%x>", preferred, available);
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
      while (*p && *p != ' ') p++;
      size_t len = p - start;

      const char *a = available;
      while (*a) {
         while (*a == ' ') {
            a++;
         }

         if (!*a) {
            break;
         }

         const char *astart = a;
         while (*a && *a != ' ') a++;
         size_t alen = a - astart;

         if (len == alen && memcmp(start, astart, len) == 0) {
            char *new_result = realloc(result, res_sz + len + 2);
            if (!new_result) {
               free(result);
               return NULL;
            }
            result = new_result;
            memcpy(result + res_sz, start, len);
            res_sz += len;
            result[res_sz++] = ' ';
            result[res_sz] = '\0';
            break; // break out of 'available' loop, continue with next preferred
         }
      }
   }

   if (res_sz > 0 && result[res_sz - 1] == ' ') {
      result[--res_sz] = 0;
   }

   Log(LOG_CRAZY, "codecneg", "codec_filter_common(|%s|, |%s|) returned |%s|", preferred, available, result);
   return result;
}
