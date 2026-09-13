// rrserver/webcam.c: Support for a webcam via gstreamer, usually pointed at rig
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

// When webcam.enable is set in the config, we grab frames from a v4l2 device
// (webcam.device, default /dev/video0) with a gstreamer pipeline, and fan the
// encoded frames out to every subscriber of the SUBSYS_VIDEO media channel.
// Clients subscribe like any other media channel; the video channel is
// announced via media.available after auth.
#ifdef USE_GSTREAMER
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

static GstElement *webcam_pipeline = NULL;
static GstElement *webcam_sink = NULL;
static struct rr_mediachan *webcam_chan = NULL;
static const char *webcam_codec = "jpeg";

// Called from the gstreamer streaming thread for each captured frame
static GstFlowReturn webcam_frame_cb(GstElement *sink, gpointer user_data) {
   GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink) );

   if (!sample) {
      return GST_FLOW_ERROR;
   }
   GstBuffer *buffer = gst_sample_get_buffer(sample);
   GstMapInfo map;

   if (!gst_buffer_map(buffer, &map, GST_MAP_READ) || map.size == 0) {
      gst_sample_unref(sample);
      return GST_FLOW_OK;
   }
   // Fan out to every client subscribed to the video channel; the media
   // channel layer owns the wire header (PARITY: librrprotocol/ws.mediachan.c)
   if (webcam_chan) {
      ws_media_broadcast_subscribed(webcam_chan, map.data, map.size, webcam_codec);
   }
   gst_buffer_unmap(buffer, &map);
   gst_sample_unref(sample);

   return GST_FLOW_OK;
}
#endif // USE_GSTREAMER

// Here we deal with fwdsp -v -t supplied frames for webcams
const char *webcam_common_codecs(const char *our_codecs, const char *cli_codecs) {
   if (!our_codecs) {
      Log(LOG_CRIT, "webcam",
         "webcam_common_codecs: You should probably configure some video codecs in config: codecs.allowed.video, returning no codecs");
      return NULL;
   }

   if (!cli_codecs) {
      // XXX: Send a notice to the user that their client is misconfigured
      Log(LOG_DEBUG, "webcam", "webcam_common_codecs: Client sent an empty video codec list");
      return NULL;
   }

   // Find the overlap between our preferred codecs and what the client supports
   // XXX: Ensure that we only return codecs with pipelines configured
   // No matches
   return NULL;
}

#ifdef USE_GSTREAMER
// Provision the video media channel and start the v4l2 capture pipeline
void webcam_init(void) {
   bool enabled = cfg_get_bool("webcam.enable", false);

   if (!enabled) {
      return;
   }
   const char *device = cfg_get("webcam.device");

   if (!device || device[0] == '\0') {
      device = "/dev/video0";
   }
   const char *codec = cfg_get("webcam.codec");

   if (codec && strlen(codec) == 4) {
      webcam_codec = codec;
   }
   // One RX video channel; vfo/rig NA since a webcam isn't tied to a rig
   webcam_chan = media_chan_add(RR_BINFRAME_SUBSYS_VIDEO, RR_BINFRAME_DIR_RX,
      RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, webcam_codec, "Webcam video");

   if (!webcam_chan) {
      Log(LOG_CRIT, "webcam", "Failed to provision video media channel");
      return;
   }
   // Simple pipeline: v4l2 device -> jpeg frames into appsink
   char pipeline_str[256];

   snprintf(pipeline_str, sizeof(pipeline_str),
      "v4l2src device=%s ! image/jpeg ! appsink name=wc_sink emit-signals=true sync=false",
      device);
   GError *err = NULL;

   webcam_pipeline = gst_parse_launch(pipeline_str, &err);

   if (!webcam_pipeline || err) {
      Log(LOG_CRIT, "webcam", "Failed to create webcam pipeline for %s: %s", device,
         (err ? err->message : "(null)") );

      if (err) {
         g_error_free(err);
      }
      webcam_pipeline = NULL;
      return;
   }
   webcam_sink = gst_bin_get_by_name(GST_BIN(webcam_pipeline), "wc_sink");

   if (!webcam_sink) {
      Log(LOG_CRIT, "webcam", "Webcam pipeline has no appsink?!");
      gst_object_unref(webcam_pipeline);
      webcam_pipeline = NULL;
      return;
   }
   g_signal_connect(webcam_sink, "new-sample", G_CALLBACK(webcam_frame_cb), NULL);

   if (gst_element_set_state(webcam_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      Log(LOG_CRIT, "webcam", "Failed to start webcam pipeline on %s", device);
      gst_object_unref(webcam_sink);
      gst_object_unref(webcam_pipeline);
      webcam_sink = NULL;
      webcam_pipeline = NULL;
      return;
   }
   Log(LOG_INFO, "webcam", "Webcam capture started on %s (codec %s, channel %s)",
      device, webcam_codec, webcam_chan->uuid);
}

// Stop the pipeline; used on shutdown
void webcam_shutdown(void) {
   if (!webcam_pipeline) {
      return;
   }
   gst_element_set_state(webcam_pipeline, GST_STATE_NULL);
   gst_object_unref(webcam_sink);
   gst_object_unref(webcam_pipeline);
   webcam_sink = NULL;
   webcam_pipeline = NULL;

   if (webcam_chan) {
      media_send_chan_removed_all(webcam_chan);
      media_chan_remove(webcam_chan->uuid);
      webcam_chan = NULL;
   }
}
#else // USE_GSTREAMER
void webcam_init(void) {
   Log(LOG_DEBUG, "webcam", "Built without gstreamer; webcam support disabled");
}

void webcam_shutdown(void) { }
#endif // USE_GSTREAMER
