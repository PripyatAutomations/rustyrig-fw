/* Standalone local AF spectrum regression test. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <rrclient/spectrum.af.h>
int main(void) {
   int16_t pcm[1024];
   for (unsigned i = 0; i < 1024; i++) {
      pcm[i] = (int16_t)(14000 * sin(2.0 * 3.14159265358979323846 * i / 16.0));
   }
   rr_af_spectrum_push("example.receiver", pcm, 1024);
   float bins[RR_AF_BINS];
   assert(rr_af_spectrum_read("example.receiver", bins));
   /* 16 kHz / 16 = 1000 Hz, FFT bin 32, displayed bin 16. */
   unsigned strongest = 0;
   for (unsigned i = 1; i < RR_AF_BINS; i++) {
      if (bins[i] > bins[strongest]) {
         strongest = i;
      }
   }
   assert(strongest == 16);
   rr_af_spectrum_forget("example.receiver");
   assert(!rr_af_spectrum_read("example.receiver", bins));
   puts("PASS: AF 1 kHz peak, snapshot buffering and receiver teardown");
   return 0;
}
