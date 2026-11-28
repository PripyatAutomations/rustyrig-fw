// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_VRIG_AUDIO_H
#define RRCLIENT_VRIG_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RR_VRIG_AUDIO_REQUEST "client.routing.vrig.audio.request"
#define RR_VRIG_AUDIO_RESULT "client.routing.vrig.audio.result"
typedef struct {
   unsigned slot;
   bool enabled;
} rr_vrig_audio_request_t;
typedef struct {
   rr_vrig_audio_request_t request;
   bool success;
} rr_vrig_audio_result_t;
void rr_vrig_audio_init(void);
void rr_vrig_audio_shutdown(void);
void rr_vrig_audio_request(const rr_vrig_audio_request_t *request);
#include <rrclient/audio.engine.h>
#define RR_VRIG_PCM_INPUT_MAX 32
/* Thread-safe copy of configured sources, including offline endpoints. */
size_t rr_vrig_rx_sources(unsigned slot,char (*sources)[64],size_t capacity);
bool rr_vrig_rx_wants(const char *source);
#define RR_VRIG_RX_ROUTE "client.routing.vrig.rx.route"
typedef struct {
   unsigned slot;
   char source[64];
   bool connect;
} rr_vrig_rx_route_t;
void rr_vrig_rx_request(const rr_vrig_rx_route_t *request);
void rr_vrig_rx_feed(const char *source,const void *pcm,size_t frames,const rr_pcm_stream_format_t *format);
#endif
