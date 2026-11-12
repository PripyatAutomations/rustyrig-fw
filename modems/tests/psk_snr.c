/* Standalone BPSK31 whole-message AWGN sweep; no pass threshold imposed. */
#include <psk.h>
#include "snr_noise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RATE 8000
#define FRAMES (RATE * 7)
#define TRIALS 5
static char decoded[4096];
static size_t used;
static void on_char(char c, void *u) {
   (void)u;
   if (used + 1 < sizeof(decoded)) decoded[used++] = c;
}
int main(void) {
   const char *message = "TESTTESTTEST";
   float *clean = calloc(FRAMES, sizeof(*clean));
   float *noisy = malloc(FRAMES * sizeof(*noisy));
   if (!clean || !noisy) return 1;
   psk_tx_t *tx = psk_tx_create(RATE, 1000.0f, PSK_MODE_BPSK31);
   if (!tx) return 1;
   if (psk_tx_write(tx, message, strlen(message)) != strlen(message)) return 1;
   if (psk_tx_read(tx, clean, FRAMES) != FRAMES) return 1;
   psk_tx_destroy(tx);
   double power = rr_signal_power_f32(clean, FRAMES);
   if (!(power > 0)) return 1;
   printf("BPSK31 AWGN sweep: %d trials/SNR, 8 kHz, total sample-band noise\n", TRIALS);
   puts("SNR(dB)  whole-message/trials   contains TEST/trials");
   for (int db = 10; db >= -45; db -= 5) {
      int full = 0, partial = 0;
      double sigma = sqrt(power / pow(10.0, db / 10.0));
      for (unsigned t = 0; t < TRIALS; t++) {
         rr_noise_t rng = {.state = 0xABCD1234u + t * 1299709u};
         for (size_t i = 0; i < FRAMES; i++)
            noisy[i] = clean[i] + (float)(sigma * rr_gaussian(&rng));
         psk_rx_t *rx = psk_rx_create(RATE, 1000.0f, PSK_MODE_BPSK31);
         if (!rx) return 1;
         used = 0;
         memset(decoded, 0, sizeof(decoded));
         psk_rx_set_char_callback(rx, on_char, NULL);
         for (size_t i = 0; i < FRAMES; i += 1024) {
            size_t n = FRAMES - i < 1024 ? FRAMES - i : 1024;
            psk_rx_feed(rx, noisy + i, n);
         }
         psk_rx_destroy(rx);
         decoded[used] = 0;
         if (strstr(decoded, message)) full++;
         if (strstr(decoded, "TEST")) partial++;
      }
      printf("%+7d  %2d/%d                  %2d/%d\n", db, full, TRIALS, partial, TRIALS);
   }
   free(noisy);
   free(clean);
   return 0;
}
