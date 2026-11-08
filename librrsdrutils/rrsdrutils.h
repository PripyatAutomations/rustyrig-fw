//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* Experimental RustyRig SDR utility descriptors. Not yet public ABI; do not serialize these native structs over a network. */
#ifndef RRSDRUTILS_H
#define RRSDRUTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum rr_iq_format {
   RR_IQ_CU8 = 1,               /* interleaved unsigned I/Q, offset 128 */
   RR_IQ_CS16 = 2,              /* interleaved signed I/Q */
   RR_IQ_CF32 = 3               /* interleaved float I/Q */
} rr_iq_format_t;

typedef struct rr_iq_stream_desc {
   uint32_t sample_rate_hz;
   uint64_t center_hz;
   uint32_t bandwidth_hz;
   rr_iq_format_t format;
   uint32_t channels;
} rr_iq_stream_desc_t;

typedef struct rr_iq_buffer {
   const void *samples;
   size_t frames;
   rr_iq_stream_desc_t stream;
   uint64_t first_sample;       /* position in the producer's sample clock */
   bool discontinuity;
} rr_iq_buffer_t;

typedef struct rr_slice_request {
   uint64_t center_hz;
   uint32_t bandwidth_hz;
   uint32_t output_rate_hz;
   rr_iq_format_t output_format;
} rr_slice_request_t;

/* First, conservative validation helpers. DSP execution is not implemented. */
bool rr_iq_stream_valid(const rr_iq_stream_desc_t *stream);
bool rr_slice_fits(const rr_iq_stream_desc_t *source, const rr_slice_request_t *slice);

#endif
