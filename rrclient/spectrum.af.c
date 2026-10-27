/* Local audio-frequency power spectrum. No GTK or backend dependency.
 * Latest decoded mono PCM is copied under a mutex; analysis runs only when
 * a viewer requests it. Never FFT in a codec or 20 ms playback callback. */
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <rrclient/spectrum.af.h>
#define FFT_N 512
typedef struct {
   int16_t samples[FFT_N];
   unsigned head, count;
} af_stream_t;
static GMutex af_lock;
static GHashTable *af_streams;
void rr_af_spectrum_push(const char *stream, const int16_t *pcm, size_t frames) {
   if (!stream || !*stream || !pcm || !frames) {
      return;
   }
   g_mutex_lock(&af_lock);
   if (!af_streams) {
      af_streams = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   af_stream_t *s = g_hash_table_lookup(af_streams, stream);
   if (!s) {
      s = g_new0(af_stream_t, 1);
      g_hash_table_insert(af_streams, g_strdup(stream), s);
   }
   /* Keep the freshest samples with a fixed 512-sample memory bound. */
   if (frames > FFT_N) {
      pcm += frames - FFT_N;
      frames = FFT_N;
   }
   for (size_t i = 0; i < frames; i++) {
      s->samples[s->head] = pcm[i];
      s->head = (s->head + 1) % FFT_N;
      if (s->count < FFT_N) {
         s->count++;
      }
   }
   g_mutex_unlock(&af_lock);
}
bool rr_af_spectrum_read(const char *stream, float bins[RR_AF_BINS]) {
   if (!stream || !bins) {
      return false;
   }
   int16_t input[FFT_N];
   g_mutex_lock(&af_lock);
   af_stream_t *s = af_streams ? g_hash_table_lookup(af_streams, stream) : NULL;
   if (!s || s->count < FFT_N) {
      g_mutex_unlock(&af_lock);
      return false;
   }
   for (unsigned i = 0; i < FFT_N; i++) {
      input[i] = s->samples[(s->head + i) % FFT_N];
   }
   g_mutex_unlock(&af_lock);
   /* 512-point iterative radix-2 FFT with Hann window. Keep computation
    * outside the decoder and playback threads; O(N log N) per visible pane. */
   double re[FFT_N], im[FFT_N] = {0};
   for (unsigned i = 0; i < FFT_N; i++) {
      double window = 0.5 - 0.5 * cos(2.0 * G_PI * i / (FFT_N - 1));
      re[i] = input[i] * window;
   }
   for (unsigned i = 1, j = 0; i < FFT_N; i++) {
      unsigned bit = FFT_N >> 1;
      while (j & bit) {
         j ^= bit;
         bit >>= 1;
      }
      j ^= bit;
      if (i < j) {
         double tmp = re[i];
         re[i] = re[j];
         re[j] = tmp;
      }
   }
   for (unsigned span = 2; span <= FFT_N; span <<= 1) {
      unsigned half = span >> 1;
      double step = -2.0 * G_PI / span;
      for (unsigned base = 0; base < FFT_N; base += span) {
         for (unsigned k = 0; k < half; k++) {
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
   for (unsigned k = 0; k < RR_AF_BINS; k++) {
      unsigned index = k * 2;
      double magnitude = hypot(re[index], im[index]) / (FFT_N * 8192.0);
      bins[k] = (float)(20.0 * log10(MAX(magnitude, 1e-7)));
   }
   return true;
}
void rr_af_spectrum_forget(const char *stream) {
   g_mutex_lock(&af_lock);
   if (af_streams && stream) {
      g_hash_table_remove(af_streams, stream);
   }
   g_mutex_unlock(&af_lock);
}
