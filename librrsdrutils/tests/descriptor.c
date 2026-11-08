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

   return 0;
}
