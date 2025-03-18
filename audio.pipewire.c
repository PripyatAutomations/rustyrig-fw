/*
 * This was shat out by chatgpt and modified to fit. Feel free to rewrite it!
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <pipewire/pipewire.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "audio.h"
#include "audio.pipewire.h"
#include "logger.h"

#if	defined(FEATURE_PIPEWIRE)

#if	0
/* Callback for processing RX (capture) */
static void on_rx_process(void *data) {
    au_device_t *dev = (au_device_t *)data;
    struct pw_buffer *buffer;
    if ((buffer = pw_stream_dequeue_buffer(dev->rx_stream)) == NULL) {
       return;
    }
    fwrite(buffer->buffer->datas[0].data, 1, buffer->buffer->datas[0].chunk->size, stdout);
    pw_stream_queue_buffer(dev->rx_stream, buffer);
}

/* Callback for processing TX (playback) */
static void on_tx_process(void *data) {
    au_device_t *dev = (au_device_t *)data;
    struct pw_buffer *buffer;
    if ((buffer = pw_stream_dequeue_buffer(dev->tx_stream)) == NULL) {
       return;
    }
    fread(buffer->buffer->datas[0].data, 1, buffer->buffer->datas[0].maxsize, stdin);
    buffer->buffer->datas[0].chunk->size = buffer->buffer->datas[0].maxsize;
    pw_stream_queue_buffer(dev->tx_stream, buffer);
}

au_device_t *au_init(au_backend_t backend, const char *device_name) {
    if (backend != AU_BACKEND_PIPEWIRE) {
       return NULL;
    }

    au_device_t *dev = calloc(1, sizeof(au_device_t));
    dev->backend = backend;

    pw_init(NULL, NULL);
    dev->loop = pw_loop_new(NULL);
    dev->context = pw_context_new(pw_loop_get(dev->loop), NULL, 0);
    dev->core = pw_context_connect(dev->context, NULL, 0);

    struct pw_properties *rx_props = pw_properties_new(PW_KEY_NODE_NAME, device_name ? device_name : "Audio-RX", NULL);
    struct pw_properties *tx_props = pw_properties_new(PW_KEY_NODE_NAME, device_name ? device_name : "Audio-TX", NULL);

    dev->rx_stream = pw_stream_new_simple(pw_loop_get(dev->loop), "RX Stream", rx_props,
                                          &(struct pw_stream_events){.version = PW_VERSION_STREAM_EVENTS, .process = on_rx_process}, dev);
    dev->tx_stream = pw_stream_new_simple(pw_loop_get(dev->loop), "TX Stream", tx_props,
                                          &(struct pw_stream_events){.version = PW_VERSION_STREAM_EVENTS, .process = on_tx_process}, dev);

    pw_stream_connect(dev->rx_stream, PW_DIRECTION_INPUT, PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT, NULL, 0);
    pw_stream_connect(dev->tx_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT, NULL, 0);

    return dev;
}

void au_poll(au_device_t *dev) {
    if (dev->backend == AU_BACKEND_PIPEWIRE) {
        pw_loop_iterate(dev->loop, 0);
    }
}

void au_cleanup(au_device_t *dev) {
    if (dev->backend == AU_BACKEND_PIPEWIRE) {
        pw_stream_destroy(dev->rx_stream);
        pw_stream_destroy(dev->tx_stream);
        pw_core_disconnect(dev->core);
        pw_context_destroy(dev->context);
        pw_loop_destroy(dev->loop);
    }
    free(dev);
}
#endif

#if	0
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>
#include <stdio.h>
#include <string.h>

#include "codec.h"  // For OPUS compression

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960  // 20ms frames at 48kHz

static void on_process(void *userdata) {
   struct pw_buffer *buffer;
   struct spa_buffer *spa_buffer;
   void *data;
   int size;

   struct audio_data *aud = userdata;
   if (!aud->stream) {
      return;
   }

   if (!(buffer = pw_stream_dequeue_buffer(aud->stream)))
      return;
   }
   spa_buffer = buffer->buffer;
   if (!(spa_buffer->datas[0].data)) {
      return;
   }

   data = spa_buffer->datas[0].data;
   size = spa_buffer->datas[0].chunk->size;

   // Send data to OPUS encoder
   process_audio_frame(data, size);

   pw_stream_queue_buffer(aud->stream, buffer);
}

void init_pipewire(struct audio_data *aud) {
   struct pw_properties *props;

   pw_init(NULL, NULL);
   aud->loop = pw_main_loop_new(NULL);
   aud->context = pw_context_new(pw_main_loop_get_loop(aud->loop), NULL, 0);
   aud->core = pw_context_connect(aud->context, NULL, 0);
   aud->stream = pw_stream_new_simple(
      pw_main_loop_get_loop(aud->loop),
      "Audio Capture",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                        PW_KEY_MEDIA_CATEGORY, "Capture",
                        PW_KEY_MEDIA_ROLE, "Communication",
                        NULL),
      on_process, aud);

   struct spa_pod *params[1];
   params[0] = spa_format_audio_raw_build(
      &aud->b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(
         .format = SPA_AUDIO_FORMAT_S16,
         .rate = SAMPLE_RATE,
         .channels = CHANNELS));

   pw_stream_connect(aud->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                     PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
                     params, 1);

   pw_main_loop_run(aud->loop);
}
#endif
#endif	// defined(FEATURE_PIPEWIRE)
