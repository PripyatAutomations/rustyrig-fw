//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include "rrsdrutils.h"

static bool format_valid(rr_iq_format_t format) {
   return format == RR_IQ_CU8 || format == RR_IQ_CS16 || format == RR_IQ_CF32;
}

bool rr_iq_stream_valid(const rr_iq_stream_desc_t *stream) {
   return stream && stream->sample_rate_hz && stream->channels &&
          format_valid(stream->format) && stream->bandwidth_hz && stream->bandwidth_hz <= stream->sample_rate_hz;
}

bool rr_slice_fits(const rr_iq_stream_desc_t *source, const rr_slice_request_t *slice) {
   if (!rr_iq_stream_valid(source) || !slice ||
      !format_valid(slice->output_format) || !slice->bandwidth_hz ||
      !slice->output_rate_hz || slice->output_rate_hz < slice->bandwidth_hz || slice->bandwidth_hz > source->bandwidth_hz) {
      return false;
   }

   /* These are occupied-bandwidth checks, not a substitute for anti-alias filter transition guards, device limits or actual DSP planning. */
   uint64_t delta = source->center_hz >= slice->center_hz ? source->center_hz - slice->center_hz : slice->center_hz - source->center_hz;

   return delta <= (source->bandwidth_hz - slice->bandwidth_hz) / 2;
}

size_t rr_iq_bytes_per_frame(const rr_iq_stream_desc_t *stream) {
   if (!rr_iq_stream_valid(stream)) return 0;
   size_t component = stream->format == RR_IQ_CU8 ? 1 :
                      stream->format == RR_IQ_CS16 ? 2 : 4;
   /* One complex sample carries both I and Q, per channel. */
   if (stream->channels > SIZE_MAX / (2 * component)) return 0;
   return (size_t)stream->channels * 2 * component;
}

bool rr_iq_chunk_valid(const rr_iq_stream_desc_t *stream,
                       const rr_iq_chunk_desc_t *chunk) {
   size_t bpf = rr_iq_bytes_per_frame(stream);
   if (!bpf || !chunk || !chunk->frames ||
       !chunk->encoded_bytes || !chunk->raw_bytes ||
       chunk->encoded_bytes > RR_IQ_MAX_CHUNK_BYTES ||
       chunk->raw_bytes > RR_IQ_MAX_CHUNK_BYTES ||
       chunk->frames > RR_IQ_MAX_CHUNK_BYTES / bpf ||
       chunk->raw_bytes != chunk->frames * bpf ||
       chunk->first_sample > UINT64_MAX - chunk->frames)
      return false;
   switch (chunk->codec) {
      case RR_IQ_CODEC_RAW:
         return chunk->encoded_bytes == chunk->raw_bytes;
      case RR_IQ_CODEC_LZ4:
      case RR_IQ_CODEC_ZSTD:
         return true;
      case RR_IQ_CODEC_DELTA_ZSTD:
         /* Avoid bit-irreversible float transformations. */
         return stream->format == RR_IQ_CS16;
      default:
         return false;
   }
}
