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

extern dict *cfg;		// main.c

static GstElement *pipeline = NULL;
static GstElement *appsrc = NULL;
bool audio_enabled = false;
bool gst_active = false;

bool ws_audio_init(void) {
   gst_init(NULL, NULL);

   pipeline = gst_parse_launch(
      "appsrc name=mysrc is-live=true format=time "
      "! audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved "
      "! audioconvert ! audioresample ! queue ! pulsesink device=default",
      NULL
   );

   if (!pipeline) return false;

   appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysrc");
   if (!appsrc) return false;

   g_object_set(G_OBJECT(appsrc),
                "format", GST_FORMAT_TIME,
                "is-live", TRUE,
                "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                NULL);

   gst_element_set_state(pipeline, GST_STATE_PLAYING);
   gst_debug_bin_to_dot_file(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "my_pipeline");
   return true;
}

void ws_audio_shutdown(void) {
   if (pipeline) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      pipeline = NULL;
   }
   if (appsrc) {
      gst_object_unref(appsrc);
      appsrc = NULL;
   }
}

void ws_binframe_process(const void *data, size_t len) {
   if (!appsrc || len == 0) return;

   GstBuffer *buffer = gst_buffer_new_allocate(NULL, len, NULL);
   gst_buffer_fill(buffer, 0, data, len);

   // Add PTS and duration
   static GstClockTime timestamp = 0;
   GstClockTime duration = gst_util_uint64_scale(len, GST_SECOND, 16000 * 2);  // 16-bit mono
   GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;  // or manually track PTS if needed
//   GST_BUFFER_PTS(buffer) = timestamp;
   GST_BUFFER_DURATION(buffer) = duration;
   timestamp += duration;

   GstFlowReturn ret;
   g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
   gst_buffer_unref(buffer);
}
