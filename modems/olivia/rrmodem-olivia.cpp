/* RustyRig Olivia RX worker (GPL-3.0-or-later).
 * fd 4: mono S16LE at 16 kHz, fd 1: decoded characters.
 * RX only until the server's PTT lease and TX PCM gating are implemented.
 */
#include <oliviamodem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include "../common/pcm16k.h"

static void feed(const float *buf, size_t n, void *context) {
   OliviaLib *rx = static_cast<OliviaLib *>(context);
   double pcm[1024];
   for (size_t i = 0; i < n; ++i) pcm[i] = buf[i];
   rx->feedAudio(pcm, (int)n, [](uint8_t c) {
      std::putchar(c);
      std::fflush(stdout);
   });
}
int main(int argc, char **argv) {
   OliviaConfig cfg;
   if (argc > 1) cfg.tones = std::atoi(argv[1]);
   if (argc > 2) cfg.bandwidthHz = std::atoi(argv[2]);
   if (cfg.tones != 4 && cfg.tones != 8 && cfg.tones != 16 &&
       cfg.tones != 32 && cfg.tones != 64) {
      std::fprintf(stderr, "Invalid Olivia tone count\n");
      return 2;
   }
   if (cfg.bandwidthHz != 125 && cfg.bandwidthHz != 250 &&
       cfg.bandwidthHz != 500 && cfg.bandwidthHz != 1000 &&
       cfg.bandwidthHz != 2000) {
      std::fprintf(stderr, "Invalid Olivia bandwidth\n");
      return 2;
   }
   try {
      OliviaLib rx(cfg);
      std::setvbuf(stdout, nullptr, _IONBF, 0);
      int rc = rr_modem_rx_loop(feed, &rx);
      return rc ? 1 : 0;
   } catch (const std::exception &e) {
      std::fprintf(stderr, "Olivia: %s\n", e.what());
      return 1;
   }
}
