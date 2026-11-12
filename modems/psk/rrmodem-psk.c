/* RustyRig PSK RX worker. GPL-3.0-compatible adapter.
 * Application output on fd 1; live receiver PCM arrives on fd 4.
 * No TX/PTT until rrserver's fd 3 lease and fd 5 safety path exist.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psk.h>
#include "../common/pcm16k.h"

static void decoded(char c, void *userdata) {
   (void)userdata;
   if (fputc((unsigned char)c, stdout) == EOF) return;
   fflush(stdout);
}
static void feed(const float *buf, size_t n, void *context) {
   psk_rx_feed((psk_rx_t *)context, buf, n);
}
int main(int argc, char **argv) {
   psk_mode_t mode = PSK_MODE_BPSK31;
   float carrier = 1500.0f;
   if (argc > 1) {
      const char *names[] = {"BPSK31","QPSK31","BPSK63","BPSK125","QPSK63","QPSK125"};
      int found = 0;
      for (int i = 0; i < 6; i++) if (!strcmp(argv[1], names[i])) {
         mode = (psk_mode_t)i;
         found = 1;
         break;
      }
      if (!found) { fprintf(stderr, "Invalid PSK mode: %s\n", argv[1]); return 2; }
   }
   if (argc > 2) {
      char *end = NULL;
      carrier = strtof(argv[2], &end);
      if (!end || *end || carrier < 100.0f || carrier > 3500.0f) return 2;
   }
   psk_rx_t *rx = psk_rx_create(8000, carrier, mode);
   if (!rx) { fprintf(stderr, "psk_rx_create failed\n"); return 1; }
   setvbuf(stdout, NULL, _IONBF, 0);
   psk_rx_set_char_callback(rx, decoded, NULL);
   int rc = rr_modem_rx_loop(feed, rx);
   psk_rx_destroy(rx);
   return rc ? 1 : 0;
}
