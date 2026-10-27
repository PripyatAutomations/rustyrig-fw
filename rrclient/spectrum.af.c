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
   /* 512-point Hann STFT with 128 aggregated positive-frequency bins.
    * dBFS normalization includes Hann coherent gain and quantization. */
   for (unsigned k = 0; k < RR_AF_BINS; k++) {
      double re = 0, im = 0;
      unsigned bin = k * 2;
      for (unsigned n = 0; n < FFT_N; n++) {
         double window = 0.5 - 0.5 * cos(2.0 * G_PI * n / (FFT_N - 1));
         double phase = 2.0 * G_PI * bin * n / FFT_N;
         double value = input[n] * window;
         re += value * cos(phase);
         im -= value * sin(phase);
      }
      double magnitude = sqrt(re * re + im * im) / (FFT_N * 8192.0);
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
