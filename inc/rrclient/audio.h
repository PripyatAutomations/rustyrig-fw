//
// audio.h
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_audio_h)
#define __rrclient_audio_h
#include "rustyrig/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "rustyrig/logger.h"
#include "rustyrig/dict.h"
#include "rustyrig/posix.h"
#include "rustyrig/mongoose.h"
#include "rustyrig/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"

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
   char                 *pipeline;		// contains  *must* be freed
} au_codec_mapping_t;

struct ws_frame {
  uint8_t magic[2];
  uint8_t *data;
  size_t len;
  struct ws_frame *next;
} __attribute__((packed));

struct audio_settings {
    enum au_codec	codec;
    uint32_t		sample_rate;			// sample rate in hz
    bool		active;				// Is the stream active?
};
typedef struct audio_settings audio_settings_t;

extern bool audio_enabled;
extern bool gst_active;
extern GstElement *rx_pipeline;
extern GstElement *rx_appsrc;
extern GstElement *rx_vol_gst_elem;
extern GstElement *tx_pipeline;
extern GstElement *tx_appsrc;
extern GstElement *tx_vol_gst_elem;
extern GstElement *tx_sink;
extern bool audio_init(void);

//extern void enqueue_frame(uint8_t *data, size_t len);
extern void audio_tx_free_frame(void);
extern void try_send_next_frame(struct mg_connection *c);
//extern GstFlowReturn handle_tx_sample(GstElement *sink, gpointer user_data);
extern bool ws_audio_init(void);
extern void ws_audio_shutdown(void);
extern bool audio_process_frame(const char *data, size_t len);

#endif	// !defined(__rrclient_audio_h)
