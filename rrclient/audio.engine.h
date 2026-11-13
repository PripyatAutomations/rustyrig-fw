//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* RustyRig client audio clock and PCM engine. No GTK or GStreamer dependencies. The output callback runs on the audio thread and must not access GTK or
 * unsynchronized fwdsp registry state. */
#ifndef RRCLIENT_AUDIO_ENGINE_H
#define RRCLIENT_AUDIO_ENGINE_H
#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Explicit PCM format contract for future negotiated source rates.
 * Validation accepts common rates; an individual mixer must still resample
 * upstream or reject a format it cannot consume without misclocking. */
typedef enum {
   RR_PCM_S16LE = 1,
   RR_PCM_F32LE = 2
} rr_pcm_sample_format_t;
typedef struct {
   uint32_t sample_rate_hz;
   uint8_t channels;
   rr_pcm_sample_format_t format;
} rr_pcm_stream_format_t;
bool rr_pcm_stream_format_valid(const rr_pcm_stream_format_t *format);
typedef struct rr_audio_engine rr_audio_engine_t;
typedef void (*rr_audio_output_fn)(const int16_t *stereo, size_t frames, void *user);
typedef struct {
   uint64_t blocks, late_ticks, max_late_us, skipped_blocks;
   uint64_t underrun_blocks, overflow_samples;
   uint64_t startup_blocks, rebuffer_blocks, starvation_events;
   uint64_t push_calls, pushed_samples, max_push_samples, max_queued_samples;
} rr_audio_engine_stats_t;
rr_audio_engine_t *rr_audio_engine_new(rr_audio_output_fn output, void *user);
/* Independent pre-gain tap; receives routed/mixed PCM before L/R listening gain. */
void rr_audio_engine_set_pre_gain_tap(rr_audio_engine_t *engine, rr_audio_output_fn tap, void *user);
bool rr_audio_engine_start(rr_audio_engine_t *engine);
void rr_audio_engine_stop(rr_audio_engine_t *engine);
void rr_audio_engine_free(rr_audio_engine_t *engine);
bool rr_audio_engine_add_stream(rr_audio_engine_t *engine, const char *id);
/* Fail closed until the source has been converted to this mixer's native
 * format; never mistake 48 kHz PCM for 16 kHz just because samples are S16. */
bool rr_audio_engine_add_stream_format(rr_audio_engine_t *engine, const char *id,
                                        const rr_pcm_stream_format_t *format);
bool rr_audio_engine_stream_format(rr_audio_engine_t *engine, const char *id,
                                   rr_pcm_stream_format_t *format);

void rr_audio_engine_remove_stream(rr_audio_engine_t *engine, const char *id);
bool rr_audio_engine_push(rr_audio_engine_t *engine, const char *id, const int16_t *pcm, size_t count);
bool rr_audio_engine_route(rr_audio_engine_t *engine, const char *id, char route);
void rr_audio_engine_gain(rr_audio_engine_t *engine, unsigned channel, int percent);
/* Pre-master-gain mixer input trim, independently for each destination ear.
 * 0 dB is unity and the initial setting for every new stream. */
bool rr_audio_engine_input_gain(rr_audio_engine_t *engine, const char *id, unsigned channel, double db);
double rr_audio_engine_input_gain_get(rr_audio_engine_t *engine, const char *id, unsigned channel);
/* Clear EOF state and any old PCM before beginning another finite playback. */
bool rr_audio_engine_reset_stream(rr_audio_engine_t *engine, const char *id);
/* Change a live stream's destination without flushing its buffered PCM. */
bool rr_audio_engine_route_preserve(rr_audio_engine_t *engine, const char *id, char route);
/* Allow a finite source to finish even with less than the startup prebuffer. */
void rr_audio_engine_finish_stream(rr_audio_engine_t *engine, const char *id);
size_t rr_audio_engine_pending(rr_audio_engine_t *engine, const char *id);
void rr_audio_engine_stats(rr_audio_engine_t *engine, rr_audio_engine_stats_t *out, bool reset);
#endif
