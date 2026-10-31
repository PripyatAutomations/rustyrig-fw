#include "rrsdrutils.h"

static bool format_valid(rr_iq_format_t format) {
   return format == RR_IQ_CU8 || format == RR_IQ_CS16 || format == RR_IQ_CF32;
}

bool rr_iq_stream_valid(const rr_iq_stream_desc_t *stream) {
   return stream && stream->sample_rate_hz && stream->channels &&
      format_valid(stream->format) &&
      stream->bandwidth_hz && stream->bandwidth_hz <= stream->sample_rate_hz;
}

bool rr_slice_fits(const rr_iq_stream_desc_t *source,
                   const rr_slice_request_t *slice) {
   if (!rr_iq_stream_valid(source) || !slice ||
       !format_valid(slice->output_format) || !slice->bandwidth_hz ||
       !slice->output_rate_hz || slice->output_rate_hz < slice->bandwidth_hz ||
       slice->bandwidth_hz > source->bandwidth_hz) return false;

   /* These are occupied-bandwidth checks, not a substitute for anti-alias
    * filter transition guards, device limits or actual DSP planning. */
   uint64_t delta = source->center_hz >= slice->center_hz ?
      source->center_hz - slice->center_hz :
      slice->center_hz - source->center_hz;
   return delta <= (source->bandwidth_hz - slice->bandwidth_hz) / 2;
}
