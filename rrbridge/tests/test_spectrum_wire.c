// RustyRig RF network-wire roundtrip regression (GPL-2.0-only).
// cc -std=gnu23 -I. rrbridge/tests/test_spectrum_wire.c -o /tmp/rr-rf-wire && /tmp/rr-rf-wire
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrbridge/spectrum.wire.h>
int main(void) {
   rr_bridge_spectrum_row source = {
      .magic = RR_BRIDGE_SPECTRUM_MAGIC,
      .version = 1, .bins = RR_BRIDGE_SPECTRUM_BINS,
      .low_hz = UINT64_C(100000),
      .high_hz = UINT64_C(30000000),
      .sequence = UINT32_C(0x12345678)
   };
   for (unsigned i = 0; i < RR_BRIDGE_SPECTRUM_BINS; i++)
      source.power[i] = (uint8_t)(i*31u);
   uint8_t packet[RR_RF_WIRE_LEN] = {0};
   rr_bridge_spectrum_row decoded = {0};
   assert(rr_rf_wire_pack(packet, &source));
   assert(!memcmp(packet, "RFR1", 4));
   assert(packet[24] == 0x12 && packet[27] == 0x78);
   assert(rr_rf_wire_unpack(&decoded, packet, sizeof(packet)));
   assert(rr_bridge_spectrum_valid(&decoded));
   assert(decoded.low_hz == source.low_hz && decoded.high_hz == source.high_hz);
   assert(decoded.sequence == source.sequence);
   assert(!memcmp(decoded.power, source.power, RR_BRIDGE_SPECTRUM_BINS));
   assert(!rr_rf_wire_unpack(&decoded, packet, sizeof(packet)-1));
   packet[4] = 2; /* unknown version */
   assert(!rr_rf_wire_unpack(&decoded, packet, sizeof(packet)));
   packet[4] = 0;
   packet[8] = 0xff; /* absurd RF low Hz */
   assert(!rr_rf_wire_unpack(&decoded, packet, sizeof(packet)));
   packet[8] = 0;
   packet[28] = 0; packet[31] = 0x01; /* invalid bin count */
   assert(!rr_rf_wire_unpack(&decoded, packet, sizeof(packet)));
   puts("PASS: versioned RF spectrum payload and malformed packet rejection");
   return 0;
}
