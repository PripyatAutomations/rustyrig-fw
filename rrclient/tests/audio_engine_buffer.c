/* Exercise the actual independent PCM mixer without GTK or fwdsp children.
 * Keep the startup/recovery rules deterministic rather than relying on sleep. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../audio.engine.c"

int main(void) {
   rr_audio_engine_t *engine = rr_audio_engine_new(NULL, NULL);
   assert(engine && rr_audio_engine_add_stream(engine, "receiver-a"));
   assert(rr_audio_engine_route(engine, "receiver-a", 'S'));
   int16_t pcm[3200];
   for (size_t i = 0; i < G_N_ELEMENTS(pcm); ++i) pcm[i] = 1000;
   int16_t stereo[AUDIO_BLOCK * 2];
   bool active = false;

   /* One packet does not start playback prematurely or count as lost audio. */
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 320));
   tick(engine, stereo, &active);
   assert(active && stereo[0] == 0 && stereo[200] == 0);
   rr_audio_engine_stats_t stats = {0};
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.startup_blocks == 1 && stats.underrun_blocks == 0);

   /* At 60 ms accumulated, play without waiting for the UI loop. */
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 640));
   tick(engine, stereo, &active);
   assert(active && stereo[0] == 0 && stereo[200] == 1000);
   tick(engine, stereo, &active);
   tick(engine, stereo, &active);
   tick(engine, stereo, &active); /* First genuine starvation. */
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.starvation_events == 1 && stats.underrun_blocks == 1);

   /* After a dropout, a single fresh block cannot cause sputtering. */
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 320));
   tick(engine, stereo, &active);
   assert(stereo[0] == 0 && stereo[200] == 0);
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, 640));
   tick(engine, stereo, &active);
   assert(stereo[0] == 0 && stereo[200] == 1000);
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.rebuffer_blocks == 1 && stats.starvation_events == 1);

   /* Large decoder bursts discard oldest samples, bounded to 160 ms. */
   assert(rr_audio_engine_route(engine, "receiver-a", 'S'));
   assert(rr_audio_engine_push(engine, "receiver-a", pcm, G_N_ELEMENTS(pcm)));
   stream_t *stream = find_stream(engine, "receiver-a");
   assert(stream && stream->count == AUDIO_MAX_QUEUED);
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.overflow_samples == G_N_ELEMENTS(pcm) - AUDIO_MAX_QUEUED);

   assert(rr_audio_engine_route(engine, "receiver-a", 'M'));
   tick(engine, stereo, &active);
   assert(!active);
   uint64_t before = stats.underrun_blocks;
   rr_audio_engine_stats(engine, &stats, false);
   assert(stats.underrun_blocks == before);
   rr_audio_engine_free(engine);
   puts("PASS: independent audio startup cushion, recovery, bounded backlog, stereo and mute");
   return 0;
}
