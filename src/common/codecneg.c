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
#include "../../ext/libmongoose/mongoose.h"
#include "common/config.h"
#include "common/fwdsp-shared.h"
#include "common/logger.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "common/codecneg.h"

audio_settings_t	au_rx_config, au_tx_config;

int au_codec_start(const char *id, bool is_tx) {
#if	0
   au_codec_mapping_t *c = au_codec_by_id(id);
   if (!c) return -1;

   if (c->refcount == 0)
      Log(LOG_INFO, "codec", "Starting codec %s (tx=%d)", c->magic, is_tx);

   c->refcount++;
#endif
   return 0;
}

int au_codec_stop(const char *id, bool is_tx) {
#if	0
   au_codec_mapping_t *c = au_codec_by_id(id);
   if (!c || c->refcount == 0) return -1;

   c->refcount--;
#endif
   return 0;
}

#if	0
// This is very ugly and could definitely use a rewrite. The TX path needs to move into ws.tx-audio.c
static void audio_tx_enqueue_frame(uint8_t *data, size_t len) {
  struct ws_frame *f = malloc(sizeof(*f));
  f->data = data;
  f->len = len;
  f->next = NULL;

  if (!send_queue) {
     send_queue = f;
  } else {
    struct ws_frame *last = send_queue;
    while (last->next) last = last->next;
    last->next = f;
  }
}

void audio_tx_free_frame(void) {
  if (send_queue) {
    free(send_queue->data);
    struct ws_frame *tmp = send_queue;
    send_queue = send_queue->next;
    free(tmp);
  }
}
#endif

// Generate a message with the codecs we support
char *codecneg_send_supported_codecs(const char *codecs) {
   char all_codecs[256] = {0};
   size_t offset = 0;
#if	0
   for (int i = 0; codecs[i].magic != NULL; i++) {
      if (offset + 5 >= sizeof(all_codecs))
         break;
      offset += snprintf(all_codecs + offset, sizeof(all_codecs) - offset, "%s ", codecs[i].magic);
   }

   const char *preferred = cfg_get("codecs.allowed");
   Log(LOG_DEBUG, "codecneg", "send_supported: preferred list: %s", preferred ? preferred : "NULL");
   char *filtered = codec_filter_common(preferred ? preferred : "", all_codecs);

/* -- what is this? it probably should go away */ 
   if (!filtered || !*filtered) {
#if	0
      Log(LOG_WARN, "codecneg", "No matching codecs, using all available");
      filtered = strdup(all_codecs);
#else
     Log(LOG_WARN, "codecneg", "No matching codecs in cfg:codecs.allowed, returning NULL");
     return NULL;
#endif
   }

   char msgbuf[1024];
   snprintf(msgbuf, sizeof(msgbuf), "{ \"media\": { \"cmd\": \"capab\", \"payload\": \"%s\" } }", filtered);
   Log(LOG_CRAZY, "codecneg", "codecneg_send_supported: Returning capab string: %s", msgbuf);

   free(filtered);
   return strdup(msgbuf);
#endif
   return NULL;
}

char *codecneg_send_supported_codecs_plain(const char *codecs) {
   char all_codecs[256] = {0};
   size_t offset = 0;

#if	0
   for (int i = 0; codecs[i].magic != NULL; i++) {
      if (offset + 5 >= sizeof(all_codecs))
         break;
      offset += snprintf(all_codecs + offset, sizeof(all_codecs) - offset, "%s ", codecs[i].magic);
   }

   const char *preferred = cfg_get("codecs.allowed");
   char *filtered = codec_filter_common(preferred ? preferred : "", all_codecs);

#if	0
      Log(LOG_WARN, "codecneg", "No matching codecs, using all available");
      filtered = strdup(all_codecs);
#else
     Log(LOG_WARN, "codecneg", "No matching codecs in cfg:codecs.allowed, returning NULL");
     return NULL;
#endif

   // Trim trailing space just in case
   size_t len = strlen(filtered);
   if (len > 0 && filtered[len - 1] == ' ')
      filtered[len - 1] = '\0';

   char msgbuf[1024];
   snprintf(msgbuf, sizeof(msgbuf), "+++MEDIA;cap=\"%s\"", filtered);
   Log(LOG_DEBUG, "codecneg", "Returning capab string: %s", msgbuf);

   free(filtered);
   return strdup(msgbuf);
#endif
   return NULL;
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
      while (*p == ' ') p++;
      if (!*p) break;

      const char *start = p;
      while (*p && *p != ' ') p++;
      size_t len = p - start;

      // Now scan `available` token-by-token
      const char *a = available;
      while (*a) {
         while (*a == ' ') a++;
         if (!*a) break;

         const char *astart = a;
         while (*a && *a != ' ') a++;
         size_t alen = a - astart;

         if (len == alen && memcmp(start, astart, len) == 0) {
            char *new_result = realloc(result, res_sz + len + 2);
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
