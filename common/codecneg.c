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
// This needs split out into ws.audio.c ws.tx-audio.c for the parts not-relevant to gstreamer.
// We should keep TX and RX here to make sure things stay in sync
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
//#include "rrclient/ws.h"

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

int au_codec_by_id(enum au_codec id) {
   int codec_entries = sizeof(au_codecs) / sizeof(au_codec_mapping_t);
   for (int i = 0; i < codec_entries; i++) {
      if (au_codecs[i].id == id) {
         Log(LOG_DEBUG, "ws.audio", "Got index %i", i);
         return i;
      }
   }
   return -1;
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
