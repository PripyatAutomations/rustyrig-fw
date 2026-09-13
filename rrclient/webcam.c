//
// rrclient/webcam.c: client-side webcam (v4l2) video source
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// When client.role is set to video-source in the config, we announce that
// role in the initial HELLO (PARITY: librrprotocol/ws.auth.c ws_send_hello),
// register as a media source after auth, grab frames from a v4l2 device
// (webcam.device) and push them as TX-direction SUBSYS_VIDEO binframes on
// the video media channel the server provisions.
//
// PARITY: librrprotocol/cli.main.c (video source frame routing)
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>

#ifdef USE_GSTREAMER
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#endif

extern rrconn_t *ws_conn;
extern time_t now;

#ifndef USE_GSTREAMER
// Built without gstreamer: the video source is unavailable
void webcam_client_start(void) {
   Log(LOG_DEBUG, "webcam", "Built without gstreamer; client webcam unavailable");
}

void webcam_client_stop(void) { }

void webcam_client_register_events(void) { }
#else

static GstElement *webcam_pipeline = NULL;
static GstElement *webcam_sink = NULL;
static uint32_t webcam_seq = 0;
static bool webcam_active = false;


// Register as a media source and make sure the video channel exists: a
// subscribe without a known uuid asks the server to create the channel for
// the given routing quadruple (PARITY: librrprotocol/ws.mediachan.c)
static void webcam_register_source(void) {
   if (!ws_conn) {
      return;
   }
   media_send_source(ws_conn, NULL);

   // Ask the server to create/confirm the video channel we will feed
   dict *d = dict_new();

   if (d) {
      dict_add(d, "msg.type", "media");
      dict_add(d, "media.cmd", "subscribe");
      dict_add_ulong(d, "media.subsys", RR_BINFRAME_SUBSYS_VIDEO);
      dict_add_ulong(d, "media.dir", RR_BINFRAME_DIR_TX);
      dict_add_ulong(d, "media.vfo", RR_BINFRAME_VFO_NA);
      dict_add_ulong(d, "media.rig", RR_BINFRAME_RIG_NA);
      dict_add(d, "media.codec", "jpeg");
      dict_add(d, "media.descr", "Webcam video");
      dict_add_ulong(d, "media.ts", now);
      ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   }
}

// Push one frame to the video channel as a TX-direction binframe; the
// server validates our source flag and fans it out to subscribers (PARITY:
// librrprotocol/cli.main.c - video source frame routing)
static void webcam_push_frame(const uint8_t *data, size_t len) {
   if (!ws_conn || !ws_conn->conn || !data || len == 0 ||
       len > RR_BINFRAME_MAX_PAYLOAD) {
      return;
   }
   const char codec[4] = { 'j', 'p', 'e', 'g' };
   uint8_t *frame = NULL;
   int flen = rr_binframe_frame(&frame, RR_BINFRAME_SUBSYS_VIDEO, codec,
      RR_BINFRAME_DIR_TX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA,
      RR_BINFRAME_STREAM_NONE, ++webcam_seq, mono_us(), data, len);

   if (flen < 0) {
      return;
   }
#ifdef USE_MONGOOSE
   mg_ws_send(ws_conn->conn, frame, flen, WEBSOCKET_OP_BINARY);
#endif
   free(frame);
}

// Called from the gstreamer streaming thread per captured frame
static GstFlowReturn webcam_frame_cb(GstElement *sink, gpointer user_data) {
   GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink) );

   if (!sample) {
      return GST_FLOW_ERROR;
   }
   GstBuffer *buffer = gst_sample_get_buffer(sample);
   GstMapInfo map;

   if (gst_buffer_map(buffer, &map, GST_MAP_READ) && map.size > 0) {
      webcam_push_frame(map.data, map.size);
      gst_buffer_unmap(buffer, &map);
   }
   gst_sample_unref(sample);

   return GST_FLOW_OK;
}

// Start grabbing frames (called after auth when we're a video source)
void webcam_client_start(void) {
   if (webcam_active || !ws_conn) {
      return;
   }
   const char *device = cfg_get("webcam.device");

   if (!device || device[0] == '\0') {
      device = "/dev/video0";
   }
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
   webcam_active = true;
   webcam_register_source();
   Log(LOG_INFO, "webcam", "Client webcam capture started on %s", device);
}

void webcam_client_stop(void) {
   if (!webcam_pipeline) {
      return;
   }
   gst_element_set_state(webcam_pipeline, GST_STATE_NULL);
   gst_object_unref(webcam_sink);
   gst_object_unref(webcam_pipeline);
   webcam_sink = NULL;
   webcam_pipeline = NULL;
   webcam_active = false;
}

// Event: auth state changed; when we're configured as a video source, start
// the capture once we're authorized
static void webcam_conn_event(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   (void)data;
   (void)cptr;
   (void)user;

   if (!event) {
      return;
   }
   if (strcasecmp(event, "authorized") == 0) {
      const char *role = cfg_get("client.role");

      if (role && strcasecmp(role, "video-source") == 0) {
         webcam_client_start();
      }
   } else if (strcasecmp(event, "disconnected") == 0) {
      webcam_client_stop();
   }
}

void webcam_client_register_events(void) {
   event_on("authorized", webcam_conn_event, NULL);
   event_on("disconnected", webcam_conn_event, NULL);
}

#endif // USE_GSTREAMER
