// librrprotocol/ws.serial.c:
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>

// Compatibility entry point; generic NMEA formatting belongs to librustyaxe.
size_t rr_gps_nmea_rmc(int32_t latitude, int32_t longitude, uint8_t flags, time_t utc, char *out, size_t capacity) {
   return rr_nmea_rmc(latitude, longitude, flags, utc, out, capacity);
}

bool rr_serial_frame_valid(const struct rr_binframe *f) {
   return f && f->hdr.subsystem == RR_BINFRAME_SUBSYS_MODEM &&
          !memcmp(f->hdr.codec, RR_SERIAL_FRAME_CODEC, 4) &&
          f->hdr.vfo == RR_BINFRAME_VFO_NA && f->hdr.rig == RR_BINFRAME_RIG_NA &&
          f->hdr.stream && f->len && f->len <= RR_SERIAL_BLOCK_MAX && (f->hdr.direction == RR_BINFRAME_DIR_TX || f->hdr.direction == RR_BINFRAME_DIR_RX);
}

bool ws_handle_serial_cli_msg(rrconn_t *client, dict *message) {
   // cli.main already emitted ws.msg.serial; application owns endpoints/UI.
   return false;
}
