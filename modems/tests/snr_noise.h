/* Reproducible AWGN. SNR is signal power / noise power across the
 * sampled Nyquist bandwidth, NOT the WSJT-X 2500 Hz reference.
 */
#ifndef RR_SNR_NOISE_H
#define RR_SNR_NOISE_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t state; } rr_noise_t;
static double rr_uniform(rr_noise_t *g) {
   g->state = 1664525u * g->state + 1013904223u;
   return ((double)g->state + 1.0) / 4294967297.0;
}
static double rr_gaussian(rr_noise_t *g) {
   double u = rr_uniform(g), v = rr_uniform(g);
   return sqrt(-2.0 * log(u)) * cos(6.2831853071795864769 * v);
}
static double rr_signal_power_f32(const float *x, size_t n) {
   double sum = 0;
   for (size_t i = 0; i < n; i++) sum += (double)x[i] * x[i];
   return n ? sum / (double)n : 0;
}
static double rr_signal_power_f64(const double *x, size_t n) {
   double sum = 0;
   for (size_t i = 0; i < n; i++) sum += x[i] * x[i];
   return n ? sum / (double)n : 0;
}
#endif
