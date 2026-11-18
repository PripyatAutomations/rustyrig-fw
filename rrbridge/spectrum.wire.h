// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// RustyRig RF spectrum network payload v1 (GPL-2.0-only).
// This is separate from rrbridge's host-local native struct/seqpacket ABI.
#ifndef RRBRIDGE_SPECTRUM_WIRE_H
#define RRBRIDGE_SPECTRUM_WIRE_H
#include <rrbridge/spectrum.row.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* "RFR1", version=1; fixed offsets, network byte order. The outer RR_BINFRAME_SUBSYS_WATERFALL header carries stream identity, room admission and fanout
 * sequence. Only power bins are quantized as bytes. */
#define RR_RF_WIRE_CODEC "rf01"
#define RR_RF_WIRE_LEN (32u + RR_BRIDGE_SPECTRUM_BINS)
static inline void rr_rf_put32(uint8_t *p, uint32_t v) {
   p[0] = v >> 24;
   p[1] = v >> 16;
   p[2] = v >> 8;
   p[3] = v;
}
static inline uint32_t rr_rf_get32(const uint8_t *p) {
   return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static inline void rr_rf_put64(uint8_t *p, uint64_t v) {
   rr_rf_put32(p, (uint32_t) (v >> 32));
   rr_rf_put32(p + 4, (uint32_t) v);
}
static inline uint64_t rr_rf_get64(const uint8_t *p) {
   return (uint64_t) rr_rf_get32(p) << 32 | rr_rf_get32(p + 4);
}
static inline bool rr_rf_wire_pack(uint8_t out[RR_RF_WIRE_LEN], const rr_bridge_spectrum_row *row) {
   if (!out || !rr_bridge_spectrum_valid(row)) {
      return false;
   }
   memcpy(out, "RFR1", 4);
   rr_rf_put32(out + 4, 1);     /* version */
   rr_rf_put64(out + 8, row->low_hz);
   rr_rf_put64(out + 16, row->high_hz);
   rr_rf_put32(out + 24, row->sequence);
   rr_rf_put32(out + 28, RR_BRIDGE_SPECTRUM_BINS);
   memcpy(out + 32, row->power, RR_BRIDGE_SPECTRUM_BINS);

   return true;
}
static inline bool rr_rf_wire_unpack(rr_bridge_spectrum_row *row, const void *packet, size_t length) {
   if (!row || !packet || length != RR_RF_WIRE_LEN) {
      return false;
   }
   const uint8_t *p = packet;

   if (memcmp(p, "RFR1", 4) || rr_rf_get32(p + 4) != 1 || rr_rf_get32(p + 28) != RR_BRIDGE_SPECTRUM_BINS) {
      return false;
   }
   rr_bridge_spectrum_row r = {
      0
   };
   r.magic = RR_BRIDGE_SPECTRUM_MAGIC;
   r.version = 1;
   r.bins = RR_BRIDGE_SPECTRUM_BINS;
   r.low_hz = rr_rf_get64(p + 8);
   r.high_hz = rr_rf_get64(p + 16);
   r.sequence = rr_rf_get32(p + 24);
   memcpy(r.power, p + 32, RR_BRIDGE_SPECTRUM_BINS);

   if (!rr_bridge_spectrum_valid(&r)) {
      return false;
   }
   *row = r;

   return true;
}
#endif
