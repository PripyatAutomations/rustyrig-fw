/* Standalone asynchronous AF FFT regression test. Compile with rrclient/spectrum.af.c, GLib threads and -lm. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <glib.h>
#include <rrclient/spectrum.af.h>

static void inject(const char *name, double frequency) {
   int16_t pcm[RR_AF_FFT_N];

   for (unsigned i = 0 ; i < RR_AF_FFT_N ; i++) {
      pcm[i] = (int16_t) (14000.0 * sin(2.0 * G_PI * frequency * i / RR_AF_SAMPLE_RATE));
   }

   rr_af_spectrum_push(name, pcm, RR_AF_FFT_N);
}

static unsigned strongest(const float bins[RR_AF_BINS]) {
   unsigned peak = 0;

   for (unsigned i = 1 ; i < RR_AF_BINS ; i++) {
      if (bins[i] > bins[peak]) {
         peak = i;
      }
   }

   return peak;
}

static bool await_spectrum(const char *name, float bins[RR_AF_BINS]) {
   for (unsigned retry = 0 ; retry < 50 ; retry++) {
      if (rr_af_spectrum_read(name, bins)) {
         return true;
      }
      g_usleep(20000);
   }

   return false;
}

int main(void) {
   float bins[RR_AF_BINS];
   rr_af_spectrum_listen(true);
   inject("tone-1000", 1000.0);
   inject("tone-1006", 1006.25);
   assert(await_spectrum("tone-1000", bins));
   assert(strongest(bins) == 256);
   assert(await_spectrum("tone-1006", bins));
   assert(strongest(bins) >= 257 && strongest(bins) <= 258);
   rr_af_spectrum_forget("tone-1000");
   assert(!rr_af_spectrum_read("tone-1000", bins));
   rr_af_spectrum_listen(false);
   rr_af_spectrum_shutdown();
   puts("PASS: off-thread AF FFT resolves adjacent FT8 6.25 Hz tones");

   return 0;
}
