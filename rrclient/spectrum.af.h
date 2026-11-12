//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* Local AF spectrum snapshots; receiver-independent and GTK-free. */
#ifndef RRCLIENT_SPECTRUM_AF_H
#define RRCLIENT_SPECTRUM_AF_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#define RR_AF_SAMPLE_RATE 16000u
#define RR_AF_FFT_N 4096u
/* Full real FFT 0-8 kHz at 16 kHz PCM, 3.90625 Hz/bin (including Nyquist). */
#define RR_AF_MAX_HZ (RR_AF_SAMPLE_RATE / 2u)
#define RR_AF_BINS (RR_AF_FFT_N / 2u + 1u)
void rr_af_spectrum_listen(bool enabled);
void rr_af_spectrum_push(const char *stream, const int16_t *pcm, size_t frames);
bool rr_af_spectrum_read(const char *stream, float bins[RR_AF_BINS]);
void rr_af_spectrum_shutdown(void);
void rr_af_spectrum_forget(const char *stream);
#endif
