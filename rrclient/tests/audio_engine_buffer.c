// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Exercise the actual independent PCM mixer without GTK or fwdsp children. Keep the startup/recovery rules deterministic rather than relying on sleep. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../audio.engine.c"

static int16_t last_pre_gain;
static unsigned pre_gain_calls;
static void verify_pre_gain(const int16_t *pcm, size_t frames, void *unused) {
   (void)unused;
   assert(frames == AUDIO_BLOCK);
   last_pre_gain = pcm[200];
   ++pre_gain_calls;
}

int main(void) {
   rr_audio_engine_t *engine = rr_audio_engine_new(NULL, NULL);
   assert(engine && rr_audio_engine_add_stream(engine, "receiver-a"));
   rr_pcm_stream_format_t native = { 16000, 1, RR_PCM_S16LE };
   rr_pcm_stream_format_t wide = { 48000, 1, RR_PCM_S16LE };
   rr_pcm_stream_format_t reported = {0};
   assert(rr_pcm_stream_format_valid(&native));
   assert(rr_pcm_stream_format_valid(&wide));
   assert(rr_audio_engine_stream_format(engine, "receiver-a", &reported));
   assert(reported.sample_rate_hz == 16000 && reported.channels == 1);
   assert(rr_audio_engine_add_stream_format(engine, "wide-am", &wide));
   assert(!rr_audio_engine_push(engine, "wide-am", (const int16_t[]){1}, 1));
   assert(!rr_audio_engine_add_stream_format(engine, "receiver-a", &wide));
   assert(rr_audio_engine_add_stream_format(engine, "receiver-a", &native));
   assert(rr_audio_engine_route(engine, "receiver-a", 'S'));
   int16_t pcm[3200];

   for (size_t i = 0 ; i < G_N_ELEMENTS(pcm) ; ++i) {
      pcm[i] = 1000;
   }

   int16_t stereo[AUDIO_BLOCK * 2];
   bool active = false;

   /* One packet does not start playback prematurely or count as lost audio. */
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 320));
   tick(engine, stereo, &active);
   assert(active && stereo[0] == 0 && stereo[200] == 0);
   rr_audio_engine_stats_t stats = {
      0
   };
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.startup_blocks == 1 && stats.underrun_blocks == 0);

   /* At 80 ms accumulated, play without waiting for the UI loop. */
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 960));
   tick(engine, stereo, &active);
   assert(active && stereo[0] == 0 && stereo[200] == 1000);
   tick(engine, stereo, &active);
   tick(engine, stereo, &active);
   tick(engine, stereo, &active);
   tick(engine, stereo, &active);   /* First genuine starvation. */
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.starvation_events == 1 && stats.underrun_blocks == 1);

   /* Recovery also requires 80 ms; a single frame must remain buffered. */
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 320));
   tick(engine, stereo, &active);
   assert(stereo[0] == 0 && stereo[200] == 0);
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 960));
   tick(engine, stereo, &active);
   assert(stereo[0] == 0 && stereo[200] == 1000);
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.rebuffer_blocks == 1 && stats.starvation_events == 1);

   /* A 200 ms decoder burst is retained under the new 240 ms ceiling. */
   assert(rr_audio_engine_route(engine, "receiver-a", 'S'));
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, G_N_ELEMENTS(pcm)));
   stream_t *stream = find_stream(engine, "receiver-a");
   assert(stream && stream->count == G_N_ELEMENTS(pcm));
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.overflow_samples == 0);
   assert(stats.max_push_samples == G_N_ELEMENTS(pcm));
   assert(stats.max_queued_samples >= G_N_ELEMENTS(pcm));

   /* RX gain changes must alter speaker PCM but never the analyzer/recorder tap. */
   rr_audio_engine_set_pre_gain_tap(engine, verify_pre_gain, NULL);
   rr_audio_engine_gain(engine, 0, 0);
   rr_audio_engine_gain(engine, 1, 0);
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 1600));
   unsigned calls_before = pre_gain_calls;
   tick(engine, stereo, &active);
   assert(active && pre_gain_calls > calls_before);
   assert(last_pre_gain > 0 && stereo[200] == 0);
   rr_audio_engine_gain(engine, 0, 100);
   rr_audio_engine_gain(engine, 1, 100);
   tick(engine, stereo, &active);
   assert(last_pre_gain > 0 && stereo[200] > 0);

   assert(rr_audio_engine_route(engine, "receiver-a", 'M'));
   tick(engine, stereo, &active);
   assert(!active);
   uint64_t before = stats.underrun_blocks;
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.underrun_blocks == before);
   rr_audio_engine_free(engine);
   /* EOF with only 10 ms of audio must bypass the usual 80 ms prefill. */
   engine = rr_audio_engine_new(NULL, NULL);
   assert(rr_audio_engine_add_stream(engine, "short-recording"));
   assert(rr_audio_engine_push(engine, "short-recording", pcm, 160));
   tick(engine, stereo, &active);
   assert(rr_audio_engine_pending(engine, "short-recording") == 160);
   rr_audio_engine_finish_stream(engine, "short-recording");
   tick(engine, stereo, &active);
   assert(active && stereo[100] == 1000);
   assert(rr_audio_engine_pending(engine, "short-recording") == 0);
   rr_audio_engine_free(engine);
   /* A graph input's 0 dB trim is unity; L/R are independent. */
   engine = rr_audio_engine_new(NULL, NULL);
   assert(rr_audio_engine_add_stream(engine, "graph.receiver"));
   assert(rr_audio_engine_input_gain_get(engine, "graph.receiver", 0) == 0.0);
   assert(rr_audio_engine_input_gain_get(engine, "graph.receiver", 1) == 0.0);
   assert(rr_audio_engine_input_gain(engine, "graph.receiver", 0, -6.0));
   assert(rr_audio_engine_input_gain(engine, "graph.receiver", 1, 0.0));
   assert(!rr_audio_engine_input_gain(engine, "graph.receiver", 0, 24.0));
   rr_audio_engine_set_pre_gain_tap(engine, verify_pre_gain, NULL);
   assert(rr_audio_engine_push(engine, "graph.receiver", pcm, 1600));
   tick(engine, stereo, &active);
   /* Recording and analysis must stay at raw 1000 even when the
    * speaker's mixer input has a -6 dB trim. */
   assert(last_pre_gain == 1000);
   assert(active && stereo[200] >= 490 && stereo[200] <= 510);
   assert(stereo[201] == 1000);
   /* Mixer output and speaker sink input each have real, independently
    * controlled L/R gain stages. Raw capture/recording remains unchanged. */
   assert(rr_audio_engine_port_gain_get(engine, false, 0) == 0.);
   assert(rr_audio_engine_port_gain_get(engine, true, 0) == 0.);
   assert(rr_audio_engine_port_gain(engine, false, 0, -6.));
   assert(rr_audio_engine_port_gain(engine, true, 1, -6.));
   assert(!rr_audio_engine_port_gain(engine, false, 0, 18.));
   tick(engine, stereo, &active);
   assert(last_pre_gain == 1000);
   assert(stereo[200] >= 245 && stereo[200] <= 255);
   assert(stereo[201] >= 490 && stereo[201] <= 510);
   assert(rr_audio_engine_port_gain(engine, false, 0, 0.));
   assert(rr_audio_engine_port_gain(engine, true, 1, 0.));
   size_t queued = rr_audio_engine_pending(engine, "graph.receiver");
   assert(queued > 0);
   /* Changing a playback destination must not flush the buffered tail. */
   assert(rr_audio_engine_route_preserve(engine, "graph.receiver", 'R'));
   assert(rr_audio_engine_pending(engine, "graph.receiver") == queued);
   tick(engine, stereo, &active);
   assert(stereo[200] == 0 && stereo[201] == 1000);
   rr_audio_engine_free(engine);
   /* Opt-in 48 kHz clock: output is 960 stereo frames per 20 ms,
    * source queues retain an 80 ms prebuffer and 240 ms maximum. */
   engine = rr_audio_engine_new_rate(48000, NULL, NULL);
   assert(engine && rr_audio_engine_sample_rate(engine) == 48000);
   rr_pcm_stream_format_t wide_native = {48000, 1, RR_PCM_S16LE};
   rr_pcm_stream_format_t old_native = {16000, 1, RR_PCM_S16LE};
   assert(rr_audio_engine_add_stream_format(engine, "legacy-rate", &old_native));
   assert(rr_audio_engine_add_stream_format(engine, "wide", &wide_native));
   assert(!rr_audio_engine_add_stream_format(engine, "wide", &old_native));
   int16_t wide_pcm[4800];
   for (size_t i = 0; i < G_N_ELEMENTS(wide_pcm); ++i) wide_pcm[i] = 1200;
   int16_t wide_stereo[960 * 2];
   assert(rr_audio_engine_push(engine, "wide", wide_pcm, 960));
   tick(engine, wide_stereo, &active);
   assert(active && wide_stereo[200] == 0);
   assert(rr_audio_engine_push(engine, "wide", wide_pcm, 2880));
   tick(engine, wide_stereo, &active);
   assert(active && wide_stereo[200] == 1200 && wide_stereo[201] == 1200);
   assert(rr_audio_engine_pending(engine, "wide") == 2880);
   assert(rr_audio_engine_input_gain(engine, "wide", 0, -6.0));
   tick(engine, wide_stereo, &active);
   assert(wide_stereo[200] >= 590 && wide_stereo[200] <= 610);
   assert(wide_stereo[201] == 1200);
   rr_audio_engine_free(engine);
   /* 44.1 kHz stereo must mix at 48 kHz with its channels intact, even
    * across packet boundaries that do not align to 20 ms. */
   engine = rr_audio_engine_new_rate(48000, NULL, NULL);
   assert(engine);
   rr_pcm_stream_format_t cd = {44100, 2, RR_PCM_S16LE};
   rr_pcm_stream_format_t float_48 = {48000, 2, RR_PCM_F32LE};
   assert(rr_audio_engine_add_stream_format(engine, "broadcast", &cd));
   assert(rr_audio_engine_add_stream_format(engine, "float-source", &float_48));
   rr_pcm_stream_format_t actual = {0};
   assert(rr_audio_engine_stream_format(engine, "broadcast", &actual));
   assert(actual.sample_rate_hz == 44100 && actual.channels == 2);
   int16_t cd_frames[4410 * 2];
   for (size_t i = 0; i < 4410; i++) {
      cd_frames[i * 2] = 1400;
      cd_frames[i * 2 + 1] = -700;
   }
   assert(!rr_audio_engine_push(engine, "broadcast", cd_frames, 4410));
   assert(rr_audio_engine_push_pcm(engine, "broadcast", cd_frames, 1211));
   assert(rr_audio_engine_push_pcm(engine, "broadcast", cd_frames + 1211 * 2, 3199));
   float float_frames[4800 * 2] = {0};
   for (size_t i = 0; i < 4800; i++) {
      float_frames[2 * i] = .125f;
      float_frames[2 * i + 1] = -.0625f;
   }
   /* Keep the other source muted while testing the broadcast stereo path. */
   assert(rr_audio_engine_route(engine, "float-source", 'M'));
   assert(rr_audio_engine_push_pcm(engine, "float-source", float_frames, 4800));
   for (unsigned i = 0; i < 4; i++) {
      tick(engine, wide_stereo, &active);
      assert(active);
   }
   assert(wide_stereo[200] >= 1300 && wide_stereo[200] <= 1500);
   assert(wide_stereo[201] <= -640 && wide_stereo[201] >= -760);
   assert(rr_audio_engine_route_preserve(engine, "broadcast", 'L'));
   tick(engine, wide_stereo, &active);
   assert(wide_stereo[201] == 0 && wide_stereo[200] >= 1300);
   rr_audio_engine_free(engine);

   /* Downsampling to the legacy clock filters high-frequency content
    * before it can fold into the ham-radio audio band. */
   engine = rr_audio_engine_new(NULL, NULL);
   assert(engine && rr_audio_engine_add_stream_format(engine, "downsample", &wide));
   int16_t hf[4800];
   for (size_t i = 0; i < G_N_ELEMENTS(hf); i++)
      hf[i] = (int16_t)(16000. * sin(2. * G_PI * 11000. * i / 48000.));
   assert(rr_audio_engine_push_pcm(engine, "downsample", hf, G_N_ELEMENTS(hf)));
   stream_t *d = find_stream(engine, "downsample");
   assert(d && d->count > 1200);
   int32_t peak = 0;
   for (size_t i = 300; i < d->count; i++) {
      int v = abs(d->samples[2 * ((d->head + i) % AUDIO_CAPACITY)]);
      if (v > peak) peak = v;
   }
   assert(peak < 2200); /* >17 dB attenuation at 11 kHz */
   rr_audio_engine_free(engine);

   engine = rr_audio_engine_new_rate(44100, NULL, NULL);
   assert(engine && rr_audio_engine_sample_rate(engine) == 44100);
   assert(rr_audio_engine_add_stream_format(engine, "cd-float", &float_48));
   assert(rr_audio_engine_push_pcm(engine, "cd-float", float_frames, 4800));
   int16_t cd_stereo[882 * 2];
   tick(engine, cd_stereo, &active);
   assert(active && cd_stereo[200] > 0 && cd_stereo[201] < 0);
   rr_audio_engine_free(engine);
   puts("PASS: native 16k, 44.1/48k stereo, independent gains, bandlimited resampling, EOF");

   return 0;
}
