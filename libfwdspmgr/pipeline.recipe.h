// SPDX-License-Identifier: GPL-2.0-only
#ifndef RUSTYRIG_PIPELINE_RECIPE_H
#define RUSTYRIG_PIPELINE_RECIPE_H
/* Host-neutral pipeline description. No GTK, registry or running GStreamer objects: usable by client and server supervisors alike. */
#include <stdbool.h>
#include <stdint.h>
typedef enum {
   RR_PIPELINE_CAPTURE_MONO,
   RR_PIPELINE_PLAYBACK_STEREO
} rr_pipeline_endpoint_t;
typedef struct {
   rr_pipeline_endpoint_t endpoint;
   unsigned device_rate_hz;     /* capture-side device hint, playback rate */
   unsigned pcm_rate_hz;        /* PCM bus rate: 16000 mono or 44.1/48k stereo */
   const char *device;          /* PulseAudio/PipeWire-Pulse device; NULL = default */
} rr_pipeline_recipe_t;
/* Return newly allocated launch string (g_free), or NULL on invalid recipe. The generated pipeline is launched exclusively in a fwdsp subprocess. */
char *rr_pipeline_recipe_compile(const rr_pipeline_recipe_t *recipe);
#endif
