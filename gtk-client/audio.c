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
#include <gst/gst.h>
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
#include "inc/config.h"
#include "inc/fwdsp-shared.h"
#include "inc/logger.h"
#include "inc/posix.h"
#include "inc/util.file.h"
static GstElement *pipeline = NULL;
static GstElement *appsrc = NULL;


void ws_audio_pipeline_init(void) {
   gst_init(NULL, NULL);

   pipeline = gst_parse_launch(
      "appsrc name=src is-live=true format=bytes do-timestamp=true "
      "! oggdemux "
      "! flacdec "
      "! audioconvert "
      "! audioresample "
      "! autoaudiosink", NULL);

   appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
   if (!pipeline) {
      Log(LOG_CRIT, "ws.audio", "Failed to create pipeline");
      return;
   }

   appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
   if (!appsrc) {
      Log(LOG_CRIT, "ws.audio", "Failed to get appsrc element");
      return;
   }

   // set caps for appsrc: raw Ogg container input
   GstCaps *caps = gst_caps_new_simple("application/ogg", NULL);
   gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
   gst_caps_unref(caps);

   gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

void ws_audio_pipeline_cleanup(void) {
   if (pipeline) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      pipeline = NULL;
      appsrc = NULL;
   }
}

bool ws_binframe_process(const char *buf, size_t len) {
   if (!pipeline) {
      ws_audio_pipeline_init();
   }

   if (!appsrc) {
      Log(LOG_CRIT, "ws.audio", "No appsrc, can't decode audio");
      return false;
   }

   GstBuffer *buffer = gst_buffer_new_allocate(NULL, len, NULL);
   GstMapInfo map;
   gst_buffer_map(buffer, &map, GST_MAP_WRITE);
   memcpy(map.data, buf, len);
   gst_buffer_unmap(buffer, &map);

   GstFlowReturn ret;
   g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
   gst_buffer_unref(buffer);

   return ret == GST_FLOW_OK;
}
