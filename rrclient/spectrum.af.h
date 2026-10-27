/* Local AF spectrum snapshots; receiver-independent and GTK-free. */
#ifndef RRCLIENT_SPECTRUM_AF_H
#define RRCLIENT_SPECTRUM_AF_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#define RR_AF_SAMPLE_RATE 16000u
#define RR_AF_FFT_N 4096u
/* Display 0-4 kHz, 1024 bins at 3.90625 Hz/bin: resolves FT8 tones. */
#define RR_AF_BINS 1024
void rr_af_spectrum_listen(bool enabled);
void rr_af_spectrum_push(const char *stream, const int16_t *pcm, size_t frames);
bool rr_af_spectrum_read(const char *stream, float bins[RR_AF_BINS]);
void rr_af_spectrum_shutdown(void);
void rr_af_spectrum_forget(const char *stream);
#endif
