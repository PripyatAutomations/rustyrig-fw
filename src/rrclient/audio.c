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
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "common/config.h"
#include "../ext/libmongoose/mongoose.h"
#include "common/codecneg.h"
#include "common/logger.h"
#include "common/fwdsp-shared.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/audio.h"
#include "rrclient/ws.h"

extern dict *cfg;		// main.c
extern struct mg_connection *ws_conn, *ws_tx_conn;
extern GtkWidget *rx_vol_slider;

bool audio_enabled = false;
bool gst_active = false;
GstElement *rx_pipeline = NULL, 	*tx_pipeline = NULL;
GstElement *rx_appsrc = NULL,   	*tx_appsrc = NULL;
GstElement *rx_vol_gst_elem = NULL,	*tx_vol_gst_elem = NULL;
GstElement *rx_sink = NULL,		*tx_sink = NULL;
static struct ws_frame *send_queue = NULL;
static bool sending_in_progress = false;

void try_send_next_frame(struct mg_connection *c) {
  if (sending_in_progress || !send_queue) {
     return;
  }

  sending_in_progress = true;
  mg_ws_send(c, send_queue->data, send_queue->len, WEBSOCKET_OP_BINARY);
  // audio_tx_free_frame() will be called once send completes
}

GstFlowReturn handle_tx_sample(GstElement *sink, gpointer user_data) {
   GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
   if (!sample) {
      return GST_FLOW_ERROR;
   }

   if (!ws_tx_conn) {
      gst_sample_unref(sample);
      return GST_FLOW_OK;
   }

   GstBuffer *buffer = gst_sample_get_buffer(sample);
   GstMapInfo map;

   if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      if (map.size > 0 && map.size < 65536) {
         struct ws_frame *frame = malloc(sizeof(*frame));
         if (!frame) {
            Log(LOG_CRIT, "audio", "OOM in handle_tx_sample!");
            exit(1);
         }

         frame->data = malloc(map.size);
         frame->len = map.size;
         memcpy(frame->data, map.data, map.size);

         mg_ws_send(ws_tx_conn, frame->data, frame->len, WEBSOCKET_OP_BINARY);
//         ws_tx_conn->pfn = ws_tx_callback;
//         ws_tx_conn->fn_data = frame;
      } else {
         Log(LOG_WARN, "ws.audio", "Discarding oversized buffer: %zu bytes", map.size);
      }
      gst_buffer_unmap(buffer, &map);
   }

   gst_sample_unref(sample);
   return GST_FLOW_OK;
}

static void on_bus_message(GstBus *bus, GstMessage *msg, gpointer user_data) {
   if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
      GError *err = NULL;
      gchar *debug = NULL;
      gst_message_parse_error(msg, &err, &debug);
      g_printerr("GStreamer ERROR: %s\n", err->message);
      g_error_free(err);
      g_free(debug);
   }
}

bool audio_init(void) {
   gst_init(NULL, NULL);

   Log(LOG_INFO, "audio", "Configuring RX audio-path");
   const char *rx_pipeline_str = cfg_get("audio.pipeline.rx");

   if (!rx_pipeline_str) {
      Log(LOG_CRIT, "audio", "audio.pipeline.rx *MUST* be set in config");
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

      // apply default RX volume

      const char *cfg_rx_volume = cfg_get("audio.volume.rx");
      float vol = 0;
      Log(LOG_DEBUG, "audio", "Setting default RX volume to %s", cfg_rx_volume);
      if (cfg_rx_volume != NULL) {
         vol = atoi(cfg_rx_volume);
         g_object_set(rx_vol_gst_elem, "volume", vol / 100.0, NULL);
      }

      const char *cfg_rx_format = cfg_get("audio.pipeline.rx.format");
      int rx_format = 0;
      if (cfg_rx_format != NULL && strcasecmp(cfg_rx_format, "bytes") == 0) {
         rx_format = GST_FORMAT_BYTES;
      } else if (cfg_rx_format && strcasecmp(cfg_rx_format, "time") == 0) {
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
//      gst_debug_bin_to_dot_file(GST_BIN(tx_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "rx-pipeline");
   } else {
      Log(LOG_DEBUG, "audio", "Empty audio.pipeline.tx");
   }

#if	1 // disabled for now
   ///////////////
   Log(LOG_INFO, "audio", "Configuring TX audio-path");

   const char *tx_pipeline_str = cfg_get("audio.pipeline.tx");
   if (!tx_pipeline_str || tx_pipeline_str >= 0) {
      Log(LOG_CRIT, "audio", "audio.pipeline.tx *MUST* be set in config for transmit capabilities");
   } else {
      Log(LOG_INFO, "audio", "Launching TX pipeline: %s", tx_pipeline_str);
      tx_pipeline = gst_parse_launch(tx_pipeline_str, NULL);
      if (!tx_pipeline) {
         Log(LOG_CRIT, "audio", "Failed to connect tx_pipeline");
         return false;
      }

      // Attach stuff
//      tx_vol_gst_elem = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx-vol");
      tx_appsrc = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx-sink");
      if (!tx_appsrc) {
         Log(LOG_CRIT, "audio", "Failed to connect tx_appsrc");
         return false;
      }

      GstBus *rx_bus = gst_pipeline_get_bus(GST_PIPELINE(rx_pipeline));
      gst_bus_add_signal_watch(rx_bus);
      g_signal_connect(rx_bus, "message", G_CALLBACK(on_bus_message), NULL);
      gst_object_unref(rx_bus);

#if	0
      const char *cfg_tx_format = cfg_get("audio.pipeline.tx.format");
      int tx_format = 0;
      if (cfg_tx_format != NULL && strcasecmp(cfg_tx_format, "bytes") == 0) {
         tx_format = GST_FORMAT_BYTES;
      } else if (cfg_tx_format != NULL && strcasecmp(cfg_tx_format, "time") == 0) {
         tx_format = GST_FORMAT_TIME;
      }

      gst_object_unref(tx_bus);

      g_object_set(G_OBJECT(tx_appsrc), "format", tx_format,
                   "is-live", TRUE,
                   "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                   NULL);
#endif

      GstBus *tx_bus = gst_pipeline_get_bus(GST_PIPELINE(tx_pipeline));
      gst_bus_add_signal_watch(tx_bus);
      g_signal_connect(tx_bus, "message", G_CALLBACK(on_bus_message), NULL);

      Log(LOG_DEBUG, "audio", "tx_sink connect callback new-sample");
      tx_sink = gst_bin_get_by_name(GST_BIN(tx_pipeline), "tx-sink");
      g_signal_connect(tx_sink, "new-sample", G_CALLBACK(handle_tx_sample), NULL);

      gst_pipeline_use_clock(GST_PIPELINE(tx_pipeline), NULL);
   // XXX: Set this once our latency detector works
   //   gst_pipeline_set_latency(GST_PIPELINE(tx_pipeline), 0);
      gst_element_set_state(tx_pipeline, GST_STATE_PLAYING);
//      gst_debug_bin_to_dot_file(GST_BIN(tx_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "tx-pipeline");
   }
#endif

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

//
// Deal with a received audio frame
bool audio_process_frame(const char *data, size_t len) {
   if (!rx_appsrc || len <= 0) {
      return true;
   }

   GstBuffer *buffer = gst_buffer_new_allocate(NULL, len, NULL);
   gst_buffer_fill(buffer, 0, data, len);

   // Add PTS and duration
   static GstClockTime timestamp = 0;
   GstClockTime duration = gst_util_uint64_scale(len, GST_SECOND, 16000 * 2);  // 16-bit mono
   GST_BUFFER_PTS(buffer) = timestamp;
   GST_BUFFER_DURATION(buffer) = duration;
   timestamp += duration;

   GstFlowReturn ret;
   g_signal_emit_by_name(rx_appsrc, "push-buffer", buffer, &ret);
   gst_buffer_unref(buffer);
}


#if	0
bool send_au_control_msg(struct mg_connection *c, audio_settings_t *au) {
   if (!c || !au) {
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
#endif
