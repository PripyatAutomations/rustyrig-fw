/*
 * This was shat out by chatgpt and modified to fit. Feel free to rewrite it!
 */
#include "config.h"
#if	defined(FEATURE_PIPEWIRE)
#include <pipewire/pipewire.h>
#include <pipewire/context.h>
#include <pipewire/properties.h> // Ensure this header is included
#include <spa/param/audio/format-utils.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "audio.h"
#include "audio.pipewire.h"
#include "logger.h"
#include "codec.h"

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960  // 20ms frames at 48kHz

rr_au_pw_data_t au;

static void on_process(void *userdata) {
   struct pw_buffer *buffer;
   struct spa_buffer *spa_buffer;
   void *data;
   int size;

   rr_au_pw_data_t *aud = userdata;
   if (!aud->capture_stream) {
      return;
   }

   if (!(buffer = pw_stream_dequeue_buffer(aud->capture_stream))) {
      return;
   }

   spa_buffer = buffer->buffer;
   if (!(spa_buffer->datas[0].data)) {
      return;
   }

   data = spa_buffer->datas[0].data;
   size = spa_buffer->datas[0].chunk->size;

   // Send data to OPUS encoder
   codec_encode_frame(data, size);

   pw_stream_queue_buffer(aud->capture_stream, buffer);
}

void pipewire_init(rr_au_pw_data_t *aud) {
   pw_init(NULL, NULL);
   aud->loop = pw_loop_new(NULL);
   struct pw_context *context = pw_context_new(aud->loop, NULL, 0);
   struct pw_core *core = pw_context_connect(context, NULL, 0);

   static const struct pw_stream_events stream_events = {
      PW_VERSION_STREAM_EVENTS,
      .process = on_process,  // Hook up the callback properly
   };

   aud->capture_stream = pw_stream_new_simple(
      aud->loop,
      "Audio Capture",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                        PW_KEY_MEDIA_CATEGORY, "Capture",
                        PW_KEY_MEDIA_ROLE, "Communication",
                        PW_KEY_APP_NAME, "rustyrig",
                        NULL),
      &stream_events, aud);

   struct spa_pod *params[1];
   params[0] = spa_format_audio_raw_build(
      &(struct spa_pod_builder){0}, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(
         .format = SPA_AUDIO_FORMAT_S16,
         .rate = SAMPLE_RATE,
         .channels = CHANNELS));

   pw_stream_connect(aud->capture_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                     PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
                     (const struct spa_pod **)params, 1);

}

// Called when PipeWire requests audio data to play
static void on_playback_process(void *userdata) {
   struct pw_buffer *buffer;
   struct spa_buffer *spa_buffer;
   void *data;
   int size;

   rr_au_pw_data_t *aud = userdata;
   if (!aud->playback_stream) {
      return;
   }

   if (!(buffer = pw_stream_dequeue_buffer(aud->playback_stream))) {
      return;
   }

   spa_buffer = buffer->buffer;
   if (!(spa_buffer->datas[0].data)) {
      return;
   }

   data = spa_buffer->datas[0].data;
   size = spa_buffer->datas[0].maxsize;

   // Ask codec layer for the next decoded frame
   int written = codec_get_pcm_frame(data, size);
   if (written > 0) {
      spa_buffer->datas[0].chunk->size = written;
   } else {
      spa_buffer->datas[0].chunk->size = 0;
   }

   pw_stream_queue_buffer(aud->playback_stream, buffer);
}

// Initialize the playback stream
void pipewire_init_playback(rr_au_pw_data_t *aud) {
   static const struct pw_stream_events playback_events = {
      PW_VERSION_STREAM_EVENTS,
      .process = on_playback_process,
   };

   aud->playback_stream = pw_stream_new_simple(
      aud->loop,
      "Audio Playback",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                        PW_KEY_MEDIA_CATEGORY, "Playback",
                        PW_KEY_MEDIA_ROLE, "Communication",
                        PW_KEY_APP_NAME, "rustyrig",
                        NULL),
      &playback_events, aud);

   struct spa_pod *params[1];
   params[0] = spa_format_audio_raw_build(
      &(struct spa_pod_builder){0}, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(
         .format = SPA_AUDIO_FORMAT_S16,
         .rate = SAMPLE_RATE,
         .channels = CHANNELS));
   pw_stream_connect(aud->playback_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                     PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
                     (const struct spa_pod **)params, 1);
}

bool rr_au_pw_runloop_real(rr_au_pw_data_t *au) {
   pw_loop_iterate(au->loop, 0);  // 0 for non-blocking
   if (au->loop != NULL) {
      pw_loop_iterate(au->loop, 0);  // Non-blocking
   }

   return false;
}

bool rr_au_pw_runloop_all(void) {
   bool rv = false;
   rr_au_pw_runloop_real(&au);
   return rv;
}

#endif	// defined(FEATURE_PIPEWIRE)
