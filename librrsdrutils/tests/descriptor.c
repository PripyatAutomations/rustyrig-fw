// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include "../rrsdrutils.h"
#include <assert.h>

int main(void) {
   rr_iq_stream_desc_t source = {
      .sample_rate_hz = 2400000, .center_hz = 145000000,
      .bandwidth_hz = 2000000, .format = RR_IQ_CS16, .channels = 1
   };
   rr_slice_request_t slice = {
      .center_hz = 145100000, .bandwidth_hz = 3000,
      .output_rate_hz = 12000, .output_format = RR_IQ_CF32
   };
   assert(rr_iq_stream_valid(&source));
   assert(rr_slice_fits(&source, &slice));
   slice.center_hz = 147000000;
   assert(!rr_slice_fits(&source, &slice));
   slice.center_hz = 145100000;
   slice.output_rate_hz = 2000;
   assert(!rr_slice_fits(&source, &slice));

   rr_iq_chunk_desc_t chunk = {
      .sequence = 0, .first_sample = 1000, .frames = 320,
      .raw_bytes = 320 * 4, .encoded_bytes = 320 * 4,
      .codec = RR_IQ_CODEC_RAW
   };
   source.channels = 1;
   assert(rr_iq_bytes_per_frame(&source) == 4); /* complex int16 I + Q */
   assert(rr_iq_chunk_valid(&source, &chunk));
   chunk.codec = RR_IQ_CODEC_DELTA_ZSTD;
   chunk.encoded_bytes = 460;
   assert(rr_iq_chunk_valid(&source, &chunk));
   source.format = RR_IQ_CF32;
   assert(!rr_iq_chunk_valid(&source, &chunk));
   source.format = RR_IQ_CS16;
   chunk.raw_bytes--;
   assert(!rr_iq_chunk_valid(&source, &chunk));
   chunk.raw_bytes++;
   chunk.codec = RR_IQ_CODEC_RAW;
   assert(!rr_iq_chunk_valid(&source, &chunk)); /* cannot rename compressed as raw */
   chunk.encoded_bytes = chunk.raw_bytes;
   chunk.frames = UINT32_MAX;
   assert(!rr_iq_chunk_valid(&source, &chunk));
   return 0;
}
