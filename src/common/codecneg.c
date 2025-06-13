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
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/config.h"
#include "common/fwdsp-shared.h"
#include "common/logger.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "common/codecneg.h"

///////////////////////
// Codec Negotiation //
///////////////////////
static au_codec_mapping_t	au_codecs[] = {
    // Codec ID			// Magic	// Sample Rate	// Pipeline
    { AU_CODEC_PCM16,		"PC16",		16000,		 NULL },
    { AU_CODEC_PCM44,		"PC44",		44100,		 NULL },
    { AU_CODEC_OPUS,		"OPUS",		48000,		 NULL },
    { AU_CODEC_FLAC,		"FLAC",		44100,           NULL },
    { AU_CODEC_MULAW8,		"MU08",		8000,		 NULL },
    { AU_CODEC_MULAW16,		"MU16",		16000,		 NULL },
    { AU_CODEC_NONE,		NULL,		0,               NULL }
};
audio_settings_t	au_rx_config, au_tx_config;

au_codec_mapping_t *au_codec_find_by_magic(const char *magic) {
   au_codec_mapping_t *rp = au_codecs;
   int codec_count = 0;

   while (rp) {
      if (strncasecmp(rp->magic, magic, 4) == 0) {
         Log(LOG_DEBUG, "codecneg", "found codec at <%x> for magic '%s'", rp, magic);
         return rp;
      }
   }

   Log(LOG_WARN, "codecneg", "codec magic '%s' not found", magic);
   return NULL;
}

au_codec_mapping_t *au_codec_by_id(enum au_codec id) {
   int codec_entries = sizeof(au_codecs) / sizeof(au_codec_mapping_t);
   for (int i = 0; i < codec_entries; i++) {
      if (au_codecs[i].id == id) {
         Log(LOG_DEBUG, "ws.audio", "Got index %i", i);
         return &au_codecs[i];
      }
   }
   return NULL;
}

const char *au_codec_get_magic(int id) {
   int codec_entries = sizeof(au_codecs) / sizeof(au_codec_mapping_t);
   for (int i = 0; i < codec_entries; i++) {
      if (au_codecs[i].id == AU_CODEC_NONE && !au_codecs[i].magic) {
         break;
      }
      if (au_codecs[i].id == id) {
         return au_codecs[i].magic;
      }
   }
   return NULL;
}

int au_codec_get_id(const char *magic) {
   int codec_entries = sizeof(au_codecs) / sizeof(au_codec_mapping_t);
   for (int i = 0; i < codec_entries; i++) {
      if (strcmp(au_codecs[i].magic, magic) == 0) {
         return au_codecs[i].id;
      }
   }
   return -1;
}

uint32_t au_codec_get_samplerate(int id) {
   int codec_entries = sizeof(au_codecs) / sizeof(au_codec_mapping_t);

   for (int i = 0; i < codec_entries; i++) {
      if (au_codecs[i].id == id) {
         return au_codecs[i].sample_rate;
      }
   }
   
   return 0;
}

int au_codec_start(enum au_codec id) {
   au_codec_mapping_t *c = au_codec_by_id(id);
   if (!c) return -1;

   if (c->refcount == 0) {
      Log(LOG_INFO, "codec", "Starting pipeline for codec %s", c->magic);
      // spawn process or set up encoder instance here
      // e.g., c->pid = fork(); exec pipeline
      // OR: open pipe/socket/etc
      c->running = 1;
   }
   c->refcount++;
   return 0;
}

int au_codec_stop(enum au_codec id) {
   au_codec_mapping_t *c = au_codec_by_id(id);
   if (!c || c->refcount == 0) return -1;

   c->refcount--;
   if (c->refcount == 0 && c->running) {
      Log(LOG_INFO, "codec", "Stopping pipeline for codec %s", c->magic);
      // kill encoder or close pipe/socket
      // e.g., kill(c->pid, SIGTERM);
      c->running = 0;
   }
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
