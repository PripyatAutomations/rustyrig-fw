// gtk-client/audio.c: support for using gstreamer for audio streams
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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "inc/config.h"
#include "inc/fwdsp-shared.h"
#include "inc/logger.h"
#include "inc/posix.h"
#include "inc/util.file.h"
#include "inc/mongoose.h"

#define	AUDIO_PCM
extern dict *cfg;		// main.c
extern struct mg_connection *ws_conn;

bool audio_enabled = false;
bool gst_active = false;
GstElement *rx_pipeline = NULL;
GstElement *rx_appsrc = NULL;
GstElement *rx_vol_gst_elem = NULL;
GstElement *tx_pipeline = NULL;
GstElement *tx_appsrc = NULL;
GstElement *tx_vol_gst_elem = NULL;
GstElement *tx_sink = NULL;

GstFlowReturn handle_tx_sample(GstElement *sink, gpointer user_data) {
   GstSample *sample;
   GstBuffer *buffer;
   GstMapInfo map;

   sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
   if (!sample) return GST_FLOW_ERROR;

   buffer = gst_sample_get_buffer(sample);
   if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      Log(LOG_DEBUG, "ws.audio", "Read %li bytes", strlen(user_data));
      mg_ws_send(ws_conn, map.data, map.size, WEBSOCKET_OP_BINARY);
      gst_buffer_unmap(buffer, &map);
   } else {
      Log(LOG_DEBUG, "ws.audio", "Buffer map failed");
   }

   gst_sample_unref(sample);
   return GST_FLOW_OK;
}

bool ws_audio_init(void) {
   gst_init(NULL, NULL);

   Log(LOG_INFO, "audio", "Configuring RX audio-path");
   const char *rx_pipeline_str = dict_get(cfg, "audio.pipeline.rx", NULL);

   if (rx_pipeline_str == NULL) {
      Log(LOG_CRIT, "audio", "audio.pipeline.rx *MUST* be set in config");
      shutdown_app(0);
   } else if (strlen(rx_pipeline_str) > 0) {
      Log(LOG_INFO, "audio", "Launching RX pipeline: %s", rx_pipeline_str);
      rx_pipeline = gst_parse_launch(rx_pipeline_str, NULL);
      if (!rx_pipeline) {
         return false;
      }

      // Attach controls
      rx_vol_gst_elem = gst_bin_get_by_name(GST_BIN(rx_pipeline), "rx-vol");
      rx_appsrc = gst_bin_get_by_name(GST_BIN(rx_pipeline), "rx-src");
      if (!rx_appsrc) {
         return false;
      }

      const char *cfg_rx_format = dict_get(cfg, "audio.pipeline.rx.format", NULL);
      int rx_format = 0;
      if (strcasecmp(cfg_rx_format, "bytes") == 0) {
         rx_format = GST_FORMAT_BYTES;
      } else if (strcasecmp(cfg_rx_format, "time") == 0) {
         rx_format = GST_FORMAT_TIME;
      }

      g_object_set(G_OBJECT(rx_appsrc), "format", rx_format,
                   "is-live", TRUE,
                   "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                   NULL);
      gst_pipeline_use_clock(GST_PIPELINE(rx_pipeline), NULL);

   // XXX: Set this once our latency detector works
   //   gst_pipeline_set_latency(GST_PIPELINE(rx_pipeline), 0);
      gst_element_set_state(rx_pipeline, GST_STATE_PLAYING);
      gst_debug_bin_to_dot_file(GST_BIN(tx_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "rx-pipeline");
   } else {
      Log(LOG_DEBUG, "audio", "Empty audio.pipeline.tx");
   }

   ///////////////
   Log(LOG_INFO, "audio", "Configuring TX audio-path");
   const char *tx_pipeline_str = dict_get(cfg, "audio.pipeline.tx", NULL);
   if (tx_pipeline_str == NULL) {
      Log(LOG_CRIT, "audio", "audio.pipeline.tx *MUST* be set in config");
      shutdown_app(0);
   } else if (strlen(tx_pipeline_str) > 0) {
      Log(LOG_INFO, "audio", "Launching TX pipeline: %s", tx_pipeline_str);
      tx_pipeline = gst_parse_launch(tx_pipeline_str, NULL);
      if (!tx_pipeline) {
         return false;
      }

      // Attach stuff
      tx_vol_gst_elem = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx-vol");
      tx_appsrc = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx-src");
      if (!tx_appsrc) {
         return false;
      }

      const char *cfg_tx_format = dict_get(cfg, "audio.pipeline.tx.format", NULL);
      int tx_format = 0;
      if (strcasecmp(cfg_tx_format, "bytes") == 0) {
         tx_format = GST_FORMAT_BYTES;
      } else if (strcasecmp(cfg_tx_format, "time") == 0) {
         tx_format = GST_FORMAT_TIME;
      }

      g_object_set(G_OBJECT(tx_appsrc), "format", tx_format,
                   "is-live", TRUE,
                   "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                   NULL);
      tx_sink = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx-sink");
      g_signal_connect(tx_sink, "new-sample", G_CALLBACK(handle_tx_sample), NULL);

      gst_pipeline_use_clock(GST_PIPELINE(tx_pipeline), NULL);
   // XXX: Set this once our latency detector works
   //   gst_pipeline_set_latency(GST_PIPELINE(tx_pipeline), 0);
      gst_element_set_state(tx_pipeline, GST_STATE_PLAYING);
      gst_debug_bin_to_dot_file(GST_BIN(tx_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "tx-pipeline");
   } else {
      Log(LOG_DEBUG, "audio", "Empty audio.pipeline.tx");
   }

   return true;
}

void ws_audio_shutdown(void) {
   if (rx_pipeline) {
      gst_element_set_state(rx_pipeline, GST_STATE_NULL);
      gst_object_unref(rx_pipeline);
      rx_pipeline = NULL;
   }

   if (rx_appsrc) {
      gst_object_unref(rx_appsrc);
      rx_appsrc = NULL;
   }

   if (tx_pipeline) {
      gst_element_set_state(tx_pipeline, GST_STATE_NULL);
      gst_object_unref(tx_pipeline);
      tx_pipeline = NULL;
   }

   if (tx_appsrc) {
      gst_object_unref(tx_appsrc);
      tx_appsrc = NULL;
   }
}

void ws_binframe_process(const void *data, size_t len) {
   if (!rx_appsrc || len == 0) {
      return;
   }

   GstBuffer *buffer = gst_buffer_new_allocate(NULL, len, NULL);
   gst_buffer_fill(buffer, 0, data, len);

   // Add PTS and duration
   static GstClockTime timestamp = 0;
   GstClockTime duration = gst_util_uint64_scale(len, GST_SECOND, 16000 * 2);  // 16-bit mono
//   GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;  // or manually track PTS if needed
   GST_BUFFER_PTS(buffer) = timestamp;
   GST_BUFFER_DURATION(buffer) = duration;
   timestamp += duration;

   GstFlowReturn ret;
   g_signal_emit_by_name(rx_appsrc, "push-buffer", buffer, &ret);
   gst_buffer_unref(buffer);
}

