/* Initial RX-only worker shim: rrserver FD 4 is 16 kHz mono S16LE.
 * Downsample to 8 kHz. This is a temporary prototype; replace the
 * box filter with the negotiated band-limited PCM converter before RF use.
 * Never read rig audio from stdin: stdin belongs to ttyMODEM app commands.
 */
#ifndef RR_MODEM_PCM16K_H
#define RR_MODEM_PCM16K_H
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>

static int rr_modem_rx_loop(void (*consume)(const float *, size_t, void *),
                             void *context) {
   unsigned char buf[4096];
   float samples[1024];
   size_t have = 0;
   for (;;) {
      ssize_t nr = read(4, buf + have, sizeof(buf) - have);
      if (nr == 0) return 0;
      if (nr < 0) {
         if (errno == EINTR) continue;
         return -1;
      }
      size_t n = have + (size_t)nr;
      size_t frames = n / 4;
      for (size_t i = 0; i < frames; i++) {
         size_t j = i * 4;
         int16_t a = (int16_t)((unsigned)buf[j] | ((unsigned)buf[j+1] << 8));
         int16_t b = (int16_t)((unsigned)buf[j+2] | ((unsigned)buf[j+3] << 8));
         samples[i] = ((float)a + (float)b) / 65536.0f;
      }
      if (frames) consume(samples, frames, context);
      have = n - frames * 4;
      for (size_t i = 0; i < have; i++) buf[i] = buf[frames * 4 + i];
   }
}
#endif
