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

/* Callback for processing RX (capture) */
static void on_rx_process(void *data) {
    au_device_t *dev = (au_device_t *)data;
    struct pw_buffer *buffer;
    if ((buffer = pw_stream_dequeue_buffer(dev->rx_stream)) == NULL) return;
    fwrite(buffer->buffer->datas[0].data, 1, buffer->buffer->datas[0].chunk->size, stdout);
    pw_stream_queue_buffer(dev->rx_stream, buffer);
}

/* Callback for processing TX (playback) */
static void on_tx_process(void *data) {
    au_device_t *dev = (au_device_t *)data;
    struct pw_buffer *buffer;
    if ((buffer = pw_stream_dequeue_buffer(dev->tx_stream)) == NULL) return;
    fread(buffer->buffer->datas[0].data, 1, buffer->buffer->datas[0].maxsize, stdin);
    buffer->buffer->datas[0].chunk->size = buffer->buffer->datas[0].maxsize;
    pw_stream_queue_buffer(dev->tx_stream, buffer);
}

au_device_t *au_init(au_backend_t backend, const char *device_name) {
    if (backend != AU_BACKEND_PIPEWIRE) return NULL;
    
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
