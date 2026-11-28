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
#endif
