/* Whole-message Olivia AWGN sweep; comparable sample-band SNR to PSK.
 * No asserted threshold: report results rather than baking in assumptions.
 */
#include <oliviamodem.h>
#include "snr_noise.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>
static constexpr int TRIALS = 3;
int main(int argc, char **argv) {
   OliviaConfig cfg;
   if (argc > 1) cfg.tones = std::atoi(argv[1]);
   if (argc > 2) cfg.bandwidthHz = std::atoi(argv[2]);
   try {
      const std::string message = "TEST TEST TEST";
      OliviaLib tx(cfg);
      std::vector<double> clean = tx.modulate(message, 1500.0);
      if (clean.empty()) { std::fprintf(stderr, "Olivia TX produced no audio\n"); return 1; }
      double power = rr_signal_power_f64(clean.data(), clean.size());
      if (!(power > 0.0)) return 1;
      std::vector<double> noisy(clean.size());
      std::printf("Olivia %d/%d: %zu samples (%.1fs), %d trials/SNR, 8 kHz\n",
                  cfg.tones, cfg.bandwidthHz, clean.size(), clean.size()/8000.0, TRIALS);
      puts("SNR(dB)  full-text/trials  contains TEST/trials");
      for (int db = 10; db >= -45; db -= 5) {
         int full = 0, partial = 0;
         double sigma = std::sqrt(power / std::pow(10., db / 10.));
         for (unsigned t = 0; t < TRIALS; t++) {
            rr_noise_t rng = {.state = 0x2468ACE0u + t * 100003u};
            for (size_t i = 0; i < clean.size(); i++)
               noisy[i] = clean[i] + sigma * rr_gaussian(&rng);
            OliviaLib rx(cfg);
            std::string result;
            auto decoded = [&result](uint8_t ch) {
               if (result.size() < 32768) result.push_back((char)ch);
            };
            for (size_t i = 0; i < noisy.size(); i += 512)
               rx.feedAudio(noisy.data() + i,
                   (int)std::min((size_t)512, noisy.size() - i), decoded);
            rx.flush(decoded);
            if (result.find(message) != std::string::npos) full++;
            if (result.find("TEST") != std::string::npos) partial++;
         }
         std::printf("%+7d  %2d/%d              %2d/%d\n", db, full, TRIALS, partial, TRIALS);
      }
   } catch (const std::exception &e) {
      std::fprintf(stderr, "Olivia benchmark: %s\n", e.what());
      return 1;
   }
   return 0;
}
