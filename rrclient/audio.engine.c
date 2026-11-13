//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* Thread-owned 20 ms PCM mixer; no GTK main-context or GStreamer calls. The sink callback must own its output endpoint independently of GTK and fwdsp's process
 * registry. */
#include "audio.engine.h"
#include <string.h>
#include <math.h>
#define AUDIO_RATE 16000u
#define AUDIO_BLOCK 320u
#define AUDIO_CAPACITY 4000u
#define AUDIO_PERIOD_US 20000
/* Buffer 80 ms before startup/recovery, with a 240 ms queue ceiling. This absorbs bursty decoder output without depending on GTK scheduling. */
#define AUDIO_PREFILL (4u * AUDIO_BLOCK)
/* Allow up to 240 ms of bursty decoded PCM before dropping the oldest samples. The fixed 160 ms ceiling was discarding bursts that arrived shortly after
 * underflow, forcing unnecessary further rebuffering. */
#define AUDIO_MAX_QUEUED (12u * AUDIO_BLOCK)
#define AUDIO_FADE_IN 64u
typedef struct stream {
   struct stream *next;
   char *id;
   char route;
   rr_pcm_stream_format_t source_format;
   int16_t samples[AUDIO_CAPACITY];
   size_t head, count;
   bool primed, ever_primed, starved, finishing;
   unsigned fade_in;
   double input_gain[2];
   double input_db[2];
} stream_t;
struct rr_audio_engine {
   GMutex lock;
   GThread *thread;
   bool running;
   stream_t *streams;
   rr_audio_output_fn output;
   void *user;
   rr_audio_output_fn pre_gain_tap;
   void *pre_gain_user;
   rr_audio_engine_stats_t stats;
   int gain[2];
};
static stream_t *find_stream(rr_audio_engine_t *e, const char *id) {
   for (stream_t *s = e->streams ; s ; s = s->next) {
      if (!strcmp(s->id, id)) {
         return s;
      }
   }

   return NULL;
}

/* All per-stream state lives under e->lock; the output callback runs only after the lock is released. Never wait on fwdsp or GTK under this lock. */
static void tick(rr_audio_engine_t *e, int16_t stereo[AUDIO_BLOCK * 2], bool *active) {
   /* Analysis/recording take raw routed PCM before per-port trims and
    * master gain. Only the speaker mix consumes input gain adjustments. */
   int32_t raw_accum[AUDIO_BLOCK * 2] = {0};
   int32_t accum[AUDIO_BLOCK * 2] = {0};
   *active = false;
   g_mutex_lock(&e->lock);

   for (stream_t *s = e->streams ; s ; s = s->next) {
      if (s->route == 'M') {
         s->count = s->head = 0;
         s->primed = s->ever_primed = s->starved = false;
         continue;
      }
      *active = true;

      if (!s->primed && s->finishing && s->count) {
         s->primed = true;  /* EOF must not require another 80 ms of input. */
      }
      if (!s->primed) {
         /* Require an 80 ms cushion both at start and after starvation. The old 20 ms recovery threshold immediately exhausted on bursty PCM arrivals, causing
          * repeating short silences even with an on-time mixer clock. */
         size_t threshold = AUDIO_PREFILL;

         if (s->count < threshold) {
            /* Don't mislabel a receiver's initial startup as a dropout. Count subsequent silence separately from the audio clock. */
            if (s->ever_primed) {
               e->stats.underrun_blocks++;
               e->stats.rebuffer_blocks++;

               if (!s->starved) {
                  e->stats.starvation_events++;
               }
               s->starved = true;
            } else {
               e->stats.startup_blocks++;
            }
            continue;
         }
         s->primed = s->ever_primed = true;
         s->starved = false;
         s->fade_in = AUDIO_FADE_IN;
      }

      size_t take = MIN(s->count, AUDIO_BLOCK);

      if (take < AUDIO_BLOCK && !s->finishing) {
         e->stats.underrun_blocks++;

         if (!s->starved) {
            e->stats.starvation_events++;
         }
         s->starved = true;
         s->primed = false;     /* Refill before resuming, rather than sputtering. */
      }

      for (size_t i = 0 ; i < take ; i++) {
         int32_t sample = s->samples[(s->head + i) % AUDIO_CAPACITY];

         if (s->fade_in) {
            sample = sample * (int32_t) (AUDIO_FADE_IN - s->fade_in) / (int32_t) AUDIO_FADE_IN;
            s->fade_in--;
         }

         /* If the last delivered buffer is short, fade its tail to silence instead of abruptly cutting a nonzero PCM sample. */
         if (take < AUDIO_BLOCK && take > 1 && i >= take - MIN(take, AUDIO_FADE_IN)) {
            sample = sample * (int32_t) (take - i - 1) / (int32_t) MIN(take, AUDIO_FADE_IN);
         }

         if (s->route != 'R') {
            raw_accum[2 * i] += sample;
            accum[2 * i] += (int32_t)(sample * s->input_gain[0]);
         }

         if (s->route != 'L') {
            raw_accum[2 * i + 1] += sample;
            accum[2 * i + 1] += (int32_t)(sample * s->input_gain[1]);
         }
      }

      s->head = (s->head + take) % AUDIO_CAPACITY;
      s->count -= take;
      if (!s->count && s->finishing) s->primed = false;
   }

   e->stats.blocks++;
   int left = e->gain[0], right = e->gain[1];
   rr_audio_output_fn tap = e->pre_gain_tap;
   void *tap_user = e->pre_gain_user;
   g_mutex_unlock(&e->lock);

   /* Analysis and recording must never inherit input trims or RX listening gain.
    * Clip the raw sum for taps, and independently gain the speaker mix.
    * The tap runs outside the engine lock like the speaker callback. */
   int16_t before_gain[AUDIO_BLOCK * 2];
   for (size_t i = 0 ; i < AUDIO_BLOCK * 2 ; i++) {
      before_gain[i] = (int16_t) CLAMP(raw_accum[i], INT16_MIN, INT16_MAX);
   }
   if (*active && tap) {
      tap(before_gain, AUDIO_BLOCK, tap_user);
   }
   for (size_t i = 0 ; i < AUDIO_BLOCK * 2 ; i++) {
      int64_t scaled = (int64_t) accum[i] * ((i & 1) ? right : left) / 100;
      stereo[i] = (int16_t) CLAMP(scaled, INT16_MIN, INT16_MAX);
   }
}
static gpointer audio_thread(gpointer data) {
   rr_audio_engine_t *e = data;
   gint64 deadline = g_get_monotonic_time() + AUDIO_PERIOD_US;

   for ( ;; ) {
      gint64 now = g_get_monotonic_time();

      if (now < deadline) {
         g_usleep((gulong) (deadline - now));
      }
      now = g_get_monotonic_time();
      g_mutex_lock(&e->lock);
      bool running = e->running;

      if (now - deadline >= 5000) {
         e->stats.late_ticks++;
         e->stats.max_late_us = MAX(e->stats.max_late_us, (uint64_t) (now - deadline));
      }
      uint64_t elapsed = now > deadline ? (uint64_t) (now - deadline) / AUDIO_PERIOD_US : 0;
      e->stats.skipped_blocks += elapsed;
      g_mutex_unlock(&e->lock);

      if (!running) {
         break;
      }
      deadline += (elapsed + 1) * AUDIO_PERIOD_US;
      int16_t pcm[AUDIO_BLOCK * 2];
      bool active;
      tick(e, pcm, &active);

      if (active && e->output) {
         e->output(pcm, AUDIO_BLOCK, e->user);
      }
   }

   return NULL;
}
rr_audio_engine_t *rr_audio_engine_new(rr_audio_output_fn fn, void *user) {
   rr_audio_engine_t *e = g_new0(rr_audio_engine_t, 1);
   g_mutex_init(&e->lock);
   e->gain[0] = e->gain[1] = 100;
   e->output = fn;
   e->user = user;

   return e;
}
void rr_audio_engine_set_pre_gain_tap(rr_audio_engine_t *e, rr_audio_output_fn tap, void *user) {
   if (!e) return;
   g_mutex_lock(&e->lock);
   e->pre_gain_tap = tap;
   e->pre_gain_user = user;
   g_mutex_unlock(&e->lock);
}
bool rr_audio_engine_start(rr_audio_engine_t *e) {
   if (!e) {
      return false;
   }
   g_mutex_lock(&e->lock);

   if (e->thread) {
      g_mutex_unlock(&e->lock);

      return true;
   }
   e->running = true;
   e->thread = g_thread_try_new("rr-audio", audio_thread, e, NULL);

   if (!e->thread) {
      e->running = false;
   }
   bool ok = e->thread != NULL;
   g_mutex_unlock(&e->lock);

   return ok;
}
void rr_audio_engine_stop(rr_audio_engine_t *e) {
   if (!e) {
      return;
   }
   g_mutex_lock(&e->lock);
   e->running = false;
   GThread *thread = e->thread;
   e->thread = NULL;
   g_mutex_unlock(&e->lock);

   if (thread) {
      g_thread_join(thread);
   }
}
void rr_audio_engine_free(rr_audio_engine_t *e) {
   if (!e) {
      return;
   }
   rr_audio_engine_stop(e);
   stream_t *s = e->streams;
   while (s) {
      stream_t *next = s->next;
      g_free(s->id);
      g_free(s);
      s = next;
   }
   g_mutex_clear(&e->lock);
   g_free(e);
}
bool rr_pcm_stream_format_valid(const rr_pcm_stream_format_t *format) {
   return format && format->sample_rate_hz >= 8000 &&
      format->sample_rate_hz <= 192000 &&
      (format->channels == 1 || format->channels == 2) &&
      (format->format == RR_PCM_S16LE || format->format == RR_PCM_F32LE);
}
bool rr_audio_engine_add_stream_format(rr_audio_engine_t *e, const char *id,
                                       const rr_pcm_stream_format_t *format) {
   /* Current output path is 16 kHz mono-per-input. Reject any other
    * rate/layout rather than silently clocking it incorrectly. */
   if (!e || !id || !*id || !rr_pcm_stream_format_valid(format) ||
       format->sample_rate_hz != AUDIO_RATE || format->channels != 1 ||
       format->format != RR_PCM_S16LE)
      return false;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   bool present = s != NULL;
   if (present) {
      if (memcmp(&s->source_format, format, sizeof(*format))) {
         g_mutex_unlock(&e->lock);
         return false;
      }
   } else {
      s = g_new0(stream_t, 1);
      s->id = g_strdup(id);
      s->source_format = *format;
      s->route = 'S';
      s->input_gain[0] = s->input_gain[1] = 1.0;
      s->next = e->streams;
      e->streams = s;
   }
   g_mutex_unlock(&e->lock);
   return true;
}
bool rr_audio_engine_add_stream(rr_audio_engine_t *e, const char *id) {
   const rr_pcm_stream_format_t native = { AUDIO_RATE, 1, RR_PCM_S16LE };
   return rr_audio_engine_add_stream_format(e, id, &native);
}
bool rr_audio_engine_stream_format(rr_audio_engine_t *e, const char *id,
                                   rr_pcm_stream_format_t *format) {
   if (!e || !id || !format) return false;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   if (s) *format = s->source_format;
   g_mutex_unlock(&e->lock);
   return s != NULL;
}
void rr_audio_engine_remove_stream(rr_audio_engine_t *e, const char *id) {
   if (!e || !id) {
      return;
   }
   g_mutex_lock(&e->lock);
   stream_t **slot = &e->streams;
   while (*slot) {
      if (!strcmp((*slot)->id, id)) {
         stream_t *s = *slot;
         *slot = s->next;
         g_free(s->id);
         g_free(s);
         break;
      }
      slot = &(*slot)->next;
   }
   g_mutex_unlock(&e->lock);
}
bool rr_audio_engine_push(rr_audio_engine_t *e, const char *id, const int16_t *pcm, size_t count) {
   if (!e || !id || !pcm || !count) {
      return false;
   }
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);

   if (s) {
      e->stats.push_calls++;
      e->stats.pushed_samples += count;
      e->stats.max_push_samples = MAX(e->stats.max_push_samples, count);

      if (count >= AUDIO_MAX_QUEUED) {
         e->stats.overflow_samples += s->count + count - AUDIO_MAX_QUEUED;
         pcm += count - AUDIO_MAX_QUEUED;
         count = AUDIO_MAX_QUEUED;
         s->head = s->count = 0;
      } else if (s->count + count > AUDIO_MAX_QUEUED) {
         size_t drop = s->count + count - AUDIO_MAX_QUEUED;
         e->stats.overflow_samples += drop;
         s->head = (s->head + drop) % AUDIO_CAPACITY;
         s->count -= drop;
      }
      size_t tail = (s->head + s->count) % AUDIO_CAPACITY;
      size_t first = MIN(count, AUDIO_CAPACITY - tail);
      memcpy(s->samples + tail, pcm, first * sizeof(*pcm));

      if (count > first) {
         memcpy(s->samples, pcm + first, (count - first) * sizeof(*pcm));
      }
      s->count += count;
      e->stats.max_queued_samples = MAX(e->stats.max_queued_samples, s->count);
   }
   g_mutex_unlock(&e->lock);

   return s != NULL;
}
bool rr_audio_engine_route(rr_audio_engine_t *e, const char *id, char route) {
   if (!e || !id || !strchr("LRSM", route)) {
      return false;
   }
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);

   if (s) {
      s->route = route;
      s->count = s->head = 0;
      s->primed = s->ever_primed = s->starved = false;
      s->fade_in = 0;
   }
   g_mutex_unlock(&e->lock);

   return s != NULL;
}
void rr_audio_engine_stats(rr_audio_engine_t *e, rr_audio_engine_stats_t *out, bool reset) {
   if (!e || !out) {
      return;
   }
   g_mutex_lock(&e->lock);
   *out = e->stats;

   if (reset) {
      memset(&e->stats, 0, sizeof(e->stats));
   }
   g_mutex_unlock(&e->lock);
}

void rr_audio_engine_finish_stream(rr_audio_engine_t *e, const char *id) {
   if (!e || !id) return;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   if (s) s->finishing = true;
   g_mutex_unlock(&e->lock);
}
size_t rr_audio_engine_pending(rr_audio_engine_t *e, const char *id) {
   if (!e || !id) return 0;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   size_t queued = s ? s->count : 0;
   g_mutex_unlock(&e->lock);
   return queued;
}
bool rr_audio_engine_input_gain(rr_audio_engine_t *e, const char *id,
                               unsigned channel, double db) {
   if (!e || !id || channel >= 2 || !isfinite(db) || db < -60 || db > 12)
      return false;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   if (s) {
      s->input_db[channel] = db;
      s->input_gain[channel] = pow(10.0, db / 20.0);
   }
   g_mutex_unlock(&e->lock);
   return s != NULL;
}
double rr_audio_engine_input_gain_get(rr_audio_engine_t *e, const char *id,
                                      unsigned channel) {
   if (!e || !id || channel >= 2) return 0.0;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   double db = s ? s->input_db[channel] : 0.0;
   g_mutex_unlock(&e->lock);
   return db;
}
bool rr_audio_engine_route_preserve(rr_audio_engine_t *e, const char *id, char route) {
   if (!e || !id || !strchr("LRSM", route)) return false;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   if (s) s->route = route;
   g_mutex_unlock(&e->lock);
   return s != NULL;
}
bool rr_audio_engine_reset_stream(rr_audio_engine_t *e, const char *id) {
   if (!e || !id) return false;
   g_mutex_lock(&e->lock);
   stream_t *s = find_stream(e, id);
   if (s) {
      s->finishing = s->primed = s->ever_primed = s->starved = false;
      s->head = s->count = s->fade_in = 0;
   }
   g_mutex_unlock(&e->lock);
   return s != NULL;
}
void rr_audio_engine_gain(rr_audio_engine_t *e, unsigned channel, int percent) {
   if (!e || channel > 1) {
      return;
   }
   g_mutex_lock(&e->lock);
   e->gain[channel] = CLAMP(percent, 0, 100);
   g_mutex_unlock(&e->lock);
}
