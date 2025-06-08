//
// gtk-client/ws.audio.c: Websocket audio framing / header stuff - WIP
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/util.file.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern dict *cfg;		// config.c
extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern struct mg_mgr mgr;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool server_ptt_state;

enum au_codec {
   AU_CODEC_NONE = 0,			// No codec configured
   AU_CODEC_PCM16,				// 16khz PCM
   AU_CODEC_PCM44,				// 44.1khz PCM
   AU_CODEC_OPUS,				// OPUS
   AU_CODEC_FLAC				// FLAC
};

typedef struct au_codec_mapping {
   enum au_codec	id;
   const char		*magic;
   int			sample_rate;		// -1 if variable or set later
} au_codec_mapping_t;

static au_codec_mapping_t	au_codecs[] = {
    // Codec ID			// Magic	// Sample Rate
    { AU_CODEC_PCM16,		"PCM1",		16000 },
    { AU_CODEC_PCM44,		"PCM4",		44100 },
    { AU_CODEC_OPUS,		"OPUS",		48000 },
    { AU_CODEC_FLAC,		"FLAC",		44100 },
    { AU_CODEC_NONE,		NULL,		0 },
};

struct audio_settings {
    enum au_codec	codec;
    uint32_t		sample_rate;			// sample rate in hz
    bool		active;				// Is the stream active?
};

typedef struct audio_settings audio_settings_t;
audio_settings_t	au_rx_config, au_tx_config;

static inline int au_codec_by_id(enum au_codec id) {
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
      if (au_codecs[i].id == AU_CODEC_NONE && au_codecs[i].magic == NULL) {
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

bool send_au_control_msg(struct mg_connection *c, audio_settings_t *au) {
   if (c == NULL || au == NULL) {
      Log(LOG_CRIT, "ws.audio", "send_au_control_msg: Got invalid c:<%x> or au:<%x>", c, au);
      return true;
   }

   int codec_id  = au_codec_by_id(au->codec);
   char msgbuf[1024];
   memset(msgbuf, 0, sizeof(msgbuf));
   snprintf(msgbuf, sizeof(msgbuf), "{ \"control\": { \"codec\": \"%s\", \"rate\": %d, \"active\": %s",
             au_codec_get_magic(codec_id), au_codec_get_samplerate(codec_id), (au->active ? "true" : "off"));
   return true;
}
