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
extern dict *cfg;		// main.c

bool audio_enabled = false;
bool gst_active = false;

void ws_audio_pipeline_cleanup(void) {
   if (pipeline) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      pipeline = NULL;
      appsrc = NULL;
   }
}

void ws_audio_pipeline_init(void) {
    if (pipeline) {
       return;
    }

    char *au_en_s = dict_get(cfg, "audio.disabled", "false");
    if (au_en_s != NULL) {
       if (strcasecmp(au_en_s, "true") == 0 || strcasecmp(au_en_s, "on") == 0) {
          audio_enabled = false;
          return;
       } else {
          audio_enabled = true;
       }
    }

    gst_init(NULL, NULL);

    pipeline = gst_parse_launch(
        "appsrc name=src "
        "is-live=true "
        "format=TIME "
        "stream-type=stream "
        "do-timestamp=true "
        "emit-signals=false "
        "! audio/x-raw,format=S16LE,channels=1,rate=16000 "
        "! audioconvert "
        "! audioresample ! queue "
        "! pulsesink device=default", NULL);

    if (!pipeline) {
        Log(LOG_CRIT, "ws.audio", "Failed to create pipeline");
        return;
    }

    appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    if (!appsrc) {
        Log(LOG_CRIT, "ws.audio", "Failed to get appsrc element");
        gst_object_unref(pipeline);
        pipeline = NULL;
        return;
    }

    g_object_set(appsrc,
      "emit-signals", FALSE,
      "stream-type", GST_APP_STREAM_TYPE_STREAM,
      "is-live", TRUE,
      "do-timestamp", TRUE,
      "format", GST_FORMAT_BYTES,
      NULL);

    gboolean emit_signals = FALSE;
    g_object_get(appsrc, "emit-signals", &emit_signals, NULL);
    printf("In init: emit-signals = %d\n", emit_signals);

    GstCaps *caps = gst_caps_from_string("audio/x-raw,format=S16LE,channels=1,rate=16000");
    gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
    gst_caps_unref(caps);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}


bool ws_binframe_process(const char *buf, size_t len) {
//    return true;
    if (!pipeline) {
        ws_audio_pipeline_init();
        if (!pipeline) return false;  // failed to init
    }

    if (!appsrc) {
        Log(LOG_CRIT, "ws.audio", "No appsrc, can't decode audio");
        return false;
    }

    GstBuffer *buffer = gst_buffer_new_and_alloc(len);

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        Log(LOG_CRIT, "ws.audio", "Failed to map buffer");
        return false;
    }

    memcpy(map.data, buf, len);
    gst_buffer_unmap(buffer, &map);

    GstClock *clock = gst_element_get_clock(pipeline);
    if (!clock) {
        gst_buffer_unref(buffer);
        Log(LOG_CRIT, "ws.audio", "No clock on pipeline");
        return false;
    }

    GstClockTime now = gst_clock_get_time(clock);
    GstClockTime base = gst_element_get_base_time(pipeline);
    gst_object_unref(clock);

    GST_BUFFER_PTS(buffer) = now - base;

    gboolean emit_signals = FALSE;
    g_object_get(appsrc, "emit-signals", &emit_signals, NULL);
    printf("emit-signals = %d\n", emit_signals);

    // Duration: len bytes / (sample_rate * bytes_per_sample)
    // 16-bit mono: 2 bytes/sample, sample_rate=16000
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(len, GST_SECOND, 2 * 16000);
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    gst_buffer_unref(buffer);

    if (ret != GST_FLOW_OK) {
        Log(LOG_CRIT, "ws.audio", "Failed to push buffer: %d", ret);
        return false;
    }

    return true;
}
