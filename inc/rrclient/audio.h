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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

struct ws_frame {
  uint8_t *data;
  size_t len;
  struct ws_frame *next;
};

extern bool audio_enabled;
extern bool gst_active;
extern GstElement *rx_pipeline;
extern GstElement *rx_appsrc;
extern GstElement *rx_vol_gst_elem;
extern GstElement *tx_pipeline;
extern GstElement *tx_appsrc;
extern GstElement *tx_vol_gst_elem;
extern GstElement *tx_sink;

extern void enqueue_frame(uint8_t *data, size_t len);
extern void free_sent_frame(void);
extern void try_send_next_frame(struct mg_connection *c);
//extern GstFlowReturn handle_tx_sample(GstElement *sink, gpointer user_data);
extern bool ws_audio_init(void);
extern void ws_audio_shutdown(void);
extern bool ws_binframe_process(const char *data, size_t len);

#endif	// !defined(__rrclient_audio_h)
