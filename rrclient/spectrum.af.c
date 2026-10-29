//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Demand-driven AF spectrum analyzer. PCM callbacks never wait for FFT work. Each 4096-point Hann FFT resolves 3.9 Hz bins at the 16 kHz RX PCM rate. Only 0-4
 * kHz is published to the current AF viewer. */
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <rrclient/spectrum.af.h>

typedef struct {
   int16_t pcm[RR_AF_FFT_N];
   unsigned head, count;
   float spectrum[RR_AF_BINS];
   bool ready;
} af_stream_t;

static GMutex af_lock;
static GHashTable *af_streams;
static gint af_listeners;
static gint af_stopping;
static GThread *af_worker;

static void calculate(const int16_t input[RR_AF_FFT_N], float result[RR_AF_BINS]) {
   /* All temporary FFT memory is owned by the analyzer thread. */
   double re[RR_AF_FFT_N], im[RR_AF_FFT_N] = {
      0
   };

   for (unsigned i = 0 ; i < RR_AF_FFT_N ; i++) {
      double window = 0.5 - 0.5 * cos(2.0 * G_PI * i / (RR_AF_FFT_N - 1));
      re[i] = (double)input[i] * window;
   }

   for (unsigned i = 1, j = 0 ; i < RR_AF_FFT_N ; i++) {
      unsigned bit = RR_AF_FFT_N >> 1;
      while (j & bit) {
         j ^= bit;
         bit >>= 1;
      }
      j ^= bit;

      if (i < j) {
         double value = re[i];
         re[i] = re[j];
         re[j] = value;
      }
   }

   for (unsigned span = 2 ; span <= RR_AF_FFT_N ; span <<= 1) {
      unsigned half = span >> 1;
      double step = -2.0 * G_PI / span;

      for (unsigned base = 0 ; base < RR_AF_FFT_N ; base += span) {
         for (unsigned k = 0 ; k < half ; k++) {
            double phase = step * k;
            double c = cos(phase), si = sin(phase);
            unsigned x = base + k, y = x + half;
            double vr = re[y] * c - im[y] * si;
            double vi = re[y] * si + im[y] * c;
            re[y] = re[x] - vr;
            im[y] = im[x] - vi;
            re[x] += vr;
            im[x] += vi;
         }
      }
   }

   for (unsigned k = 0 ; k < RR_AF_BINS ; k++) {
      double magnitude = hypot(re[k], im[k]) / (RR_AF_FFT_N * 8192.0);
      result[k] = (float)(20.0 * log10(MAX(magnitude, 1e-7)));
   }
}

static gpointer analyze(gpointer unused) {
   (void)unused;
   while (!g_atomic_int_get(&af_stopping)) {
      if (g_atomic_int_get(&af_listeners) > 0) {
         /* The hash table and PCM buffers may be edited by the decoder thread. Snapshot one stream under lock, compute outside it. */
         GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
         g_mutex_lock(&af_lock);

         if (af_streams) {
            GHashTableIter iter;
            gpointer key, value;
            g_hash_table_iter_init(&iter, af_streams);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
               af_stream_t *stream = value;

               if (stream->count >= RR_AF_FFT_N) {
                  g_ptr_array_add(names, g_strdup(key));
               }
            }
         }
         g_mutex_unlock(&af_lock);

         for (unsigned i = 0 ; i < names->len ; i++) {
            if (g_atomic_int_get(&af_stopping) || g_atomic_int_get(&af_listeners) <= 0) {
               break;
            }
            const char *name = g_ptr_array_index(names, i);
            int16_t pcm[RR_AF_FFT_N];
            float bins[RR_AF_BINS];
            bool present = false;
            g_mutex_lock(&af_lock);
            af_stream_t *stream = af_streams ? g_hash_table_lookup(af_streams, name) : NULL;

            if (stream && stream->count == RR_AF_FFT_N) {
               for (unsigned n = 0 ; n < RR_AF_FFT_N ; n++) {
                  pcm[n] = stream->pcm[(stream->head + n) % RR_AF_FFT_N];
               }

               present = true;
            }
            g_mutex_unlock(&af_lock);

            if (!present) {
               continue;
            }
            calculate(pcm, bins);
            g_mutex_lock(&af_lock);
            stream = af_streams ? g_hash_table_lookup(af_streams, name) : NULL;

            if (stream) {
               memcpy(stream->spectrum, bins, sizeof(bins));
               stream->ready = true;
            }
            g_mutex_unlock(&af_lock);
         }

         g_ptr_array_free(names, TRUE);
      }
      g_usleep(200000);
   }
   return NULL;
}

void rr_af_spectrum_listen(bool enabled) {
   if (enabled) {
      g_atomic_int_inc(&af_listeners);
      g_mutex_lock(&af_lock);

      if (!af_worker && !g_atomic_int_get(&af_stopping)) {
         af_worker = g_thread_new("rr-af-spectrum", analyze, NULL);
      }
      g_mutex_unlock(&af_lock);
   } else {
      g_atomic_int_add(&af_listeners, -1);
   }
}

void rr_af_spectrum_push(const char *name, const int16_t *pcm, size_t frames) {
   if (g_atomic_int_get(&af_listeners) <= 0 || g_atomic_int_get(&af_stopping) || !name || !*name || !pcm || !frames) {
      return;
   }

   /* Never block the decoder for a visualization. */
   if (!g_mutex_trylock(&af_lock)) {
      return;
   }

   if (!af_streams) {
      af_streams = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   af_stream_t *stream = g_hash_table_lookup(af_streams, name);

   if (!stream) {
      stream = g_new0(af_stream_t, 1);
      g_hash_table_insert(af_streams, g_strdup(name), stream);
   }

   if (frames > RR_AF_FFT_N) {
      pcm += frames - RR_AF_FFT_N;
      frames = RR_AF_FFT_N;
   }

   for (size_t i = 0 ; i < frames ; i++) {
      stream->pcm[stream->head] = pcm[i];
      stream->head = (stream->head + 1) % RR_AF_FFT_N;

      if (stream->count < RR_AF_FFT_N) {
         stream->count++;
      }
   }

   g_mutex_unlock(&af_lock);
}

bool rr_af_spectrum_read(const char *name, float bins[RR_AF_BINS]) {
   if (!name || !bins) {
      return false;
   }
   g_mutex_lock(&af_lock);
   af_stream_t *stream = af_streams ? g_hash_table_lookup(af_streams, name) : NULL;
   bool ready = stream && stream->ready;

   if (ready) {
      memcpy(bins, stream->spectrum, sizeof(stream->spectrum));
   }
   g_mutex_unlock(&af_lock);

   return ready;
}

void rr_af_spectrum_forget(const char *name) {
   g_mutex_lock(&af_lock);

   if (af_streams && name) {
      g_hash_table_remove(af_streams, name);
   }
   g_mutex_unlock(&af_lock);
}

void rr_af_spectrum_shutdown(void) {
   g_atomic_int_set(&af_stopping, 1);
   GThread *thread = af_worker;

   if (thread) {
      g_thread_join(thread);
      af_worker = NULL;
   }
   g_mutex_lock(&af_lock);

   if (af_streams) {
      g_hash_table_destroy(af_streams);
      af_streams = NULL;
   }
   g_mutex_unlock(&af_lock);
}
