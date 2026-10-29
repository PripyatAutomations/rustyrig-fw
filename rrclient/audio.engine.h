//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* RustyRig client audio clock and PCM engine. No GTK or GStreamer dependencies. The output callback runs on the audio thread and must not access GTK or
 * unsynchronized fwdsp registry state. */
#ifndef RRCLIENT_AUDIO_ENGINE_H
#define RRCLIENT_AUDIO_ENGINE_H
#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct rr_audio_engine rr_audio_engine_t;
typedef void (*rr_audio_output_fn)(const int16_t *stereo, size_t frames, void *user);
typedef struct {
   uint64_t blocks, late_ticks, max_late_us, skipped_blocks;
   uint64_t underrun_blocks, overflow_samples;
   uint64_t startup_blocks, rebuffer_blocks, starvation_events;
   uint64_t push_calls, pushed_samples, max_push_samples, max_queued_samples;
} rr_audio_engine_stats_t;
rr_audio_engine_t *rr_audio_engine_new(rr_audio_output_fn output, void *user);
bool rr_audio_engine_start(rr_audio_engine_t *engine);
void rr_audio_engine_stop(rr_audio_engine_t *engine);
void rr_audio_engine_free(rr_audio_engine_t *engine);
bool rr_audio_engine_add_stream(rr_audio_engine_t *engine, const char *id);
void rr_audio_engine_remove_stream(rr_audio_engine_t *engine, const char *id);
bool rr_audio_engine_push(rr_audio_engine_t *engine, const char *id, const int16_t *pcm, size_t count);
bool rr_audio_engine_route(rr_audio_engine_t *engine, const char *id, char route);
void rr_audio_engine_gain(rr_audio_engine_t *engine, unsigned channel, int percent);
void rr_audio_engine_stats(rr_audio_engine_t *engine, rr_audio_engine_stats_t *out, bool reset);
#endif
