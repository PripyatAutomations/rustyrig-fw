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
/* Native 16 kHz compatibility constructor; optional 44.1/48 kHz mix clock. */
rr_audio_engine_t *rr_audio_engine_new(rr_audio_output_fn output, void *user);
rr_audio_engine_t *rr_audio_engine_new_rate(uint32_t sample_rate_hz,
                                           rr_audio_output_fn output, void *user);
uint32_t rr_audio_engine_sample_rate(const rr_audio_engine_t *engine);

/* Independent pre-gain tap; receives routed/mixed PCM before L/R listening gain. */
void rr_audio_engine_set_pre_gain_tap(rr_audio_engine_t *engine, rr_audio_output_fn tap, void *user);
bool rr_audio_engine_start(rr_audio_engine_t *engine);
void rr_audio_engine_stop(rr_audio_engine_t *engine);
void rr_audio_engine_free(rr_audio_engine_t *engine);
bool rr_audio_engine_add_stream(rr_audio_engine_t *engine, const char *id);
/* Register source PCM rate, format and mono/stereo layout. A bounded
 * producer-side resampler converts input into the engine clock; the audio
 * mixer thread performs no sample-rate conversion. */
bool rr_audio_engine_add_stream_format(rr_audio_engine_t *engine, const char *id,
                                        const rr_pcm_stream_format_t *format);
bool rr_audio_engine_stream_format(rr_audio_engine_t *engine, const char *id,
                                   rr_pcm_stream_format_t *format);
/* Lock-protected producer/mixer diagnostics. Queue counts are in mixer frames,
 * independent of the source sample rate. No GTK or logging on the audio thread. */
typedef struct {
   rr_pcm_stream_format_t source;
   uint32_t mixer_rate_hz;
   size_t queued_frames;
   uint64_t dropped_frames;
   uint64_t starvation_events;
   bool resampling;
} rr_audio_stream_status_t;
bool rr_audio_engine_stream_status(rr_audio_engine_t *engine, const char *id,
                                   rr_audio_stream_status_t *status);

void rr_audio_engine_remove_stream(rr_audio_engine_t *engine, const char *id);
/* Legacy push consumes mono native-clock S16 frames only. */
bool rr_audio_engine_push(rr_audio_engine_t *engine, const char *id, const int16_t *pcm, size_t count);
/* Push interleaved PCM in the stream's REGISTERED format; frames are sample
 * frames, not individual interleaved channel values. Input is copied.
 * Format/rate are internal transport metadata, not listening codec choices. */
bool rr_audio_engine_push_pcm(rr_audio_engine_t *engine, const char *id,
                              const void *interleaved, size_t frames);
bool rr_audio_engine_route(rr_audio_engine_t *engine, const char *id, char route);
void rr_audio_engine_gain(rr_audio_engine_t *engine, unsigned channel, int percent);
/* Independent L/R trims at the mixer's output and the playback sink's
 * input. These apply after source output trims, before listening volume. */
bool rr_audio_engine_port_gain(rr_audio_engine_t *engine, bool sink_input,
                               unsigned channel, double db);
double rr_audio_engine_port_gain_get(rr_audio_engine_t *engine, bool sink_input,
                                     unsigned channel);
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
