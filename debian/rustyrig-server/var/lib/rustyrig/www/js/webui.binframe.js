//
// www/js/webui.binframe.js: parse binframe v2 binary websocket frames
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// PARITY: librrprotocol/ws.binframe.h (authoritative C)
// PARITY: librrprotocol/binframe.c
//
// Spec: doc/media-frames.md
//
// All multi-byte fields are big-endian. Header is 28 bytes packed:
//
//     off  size  field
//     0    2     magic 'R','R'
//     2    1     version (0x01)
//     3    1     subsystem
//     4    4     codec magic (ASCII, e.g. "pc16")
//     8    1     direction (0 = RX, 1 = TX)
//     9    1     vfo
//     10   1     rig
//     11   1     stream
//     12   4     seq (uint32)
//     16   4     payload_len (uint32)
//     20   8     ts (uint64 microseconds)
//     28   ...   payload
//
"use strict";

const RR_BINFRAME_MAGIC0 = 0x52;    // 'R'
const RR_BINFRAME_MAGIC1 = 0x52;    // 'R'
const RR_BINFRAME_VERSION = 0x01;
const RR_BINFRAME_HDR_LEN = 28;
const RR_BINFRAME_MAX_PAYLOAD = 65535 - RR_BINFRAME_HDR_LEN;

const RR_BINFRAME_SUBSYS_NONE = 0x00;
const RR_BINFRAME_SUBSYS_AUDIO = 0x01;
const RR_BINFRAME_SUBSYS_VIDEO = 0x02;
const RR_BINFRAME_SUBSYS_WATERFALL = 0x03;
const RR_BINFRAME_SUBSYS_MODEM = 0x04;
const RR_BINFRAME_SUBSYS_FILE = 0x05;
const RR_BINFRAME_SUBSYS_CONTROL = 0x06;
const RR_BINFRAME_SUBSYS_LOG = 0x07;
const RR_BINFRAME_SUBSYS_KEEPALIVE = 0xFF;

const RR_BINFRAME_DIR_RX = 0x00;
const RR_BINFRAME_DIR_TX = 0x01;
const RR_BINFRAME_DIR_NA = 0xFF;

// Parse an ArrayBuffer into a binframe object:
//    { subsystem, codec, dir, vfo, rig, stream, seq, payload_len, ts,
//      payload (Uint8Array of the payload bytes) }
// Returns null for legacy (non-'RR') or invalid frames; caller may then
// fall back to legacy handling.
function binframe_parse(buf) {
   if (!(buf instanceof ArrayBuffer) || buf.byteLength < RR_BINFRAME_HDR_LEN) {
      return null;
   }
   var dv = new DataView(buf);

   if (dv.getUint8(0) !== RR_BINFRAME_MAGIC0 || dv.getUint8(1) !== RR_BINFRAME_MAGIC1) {
      return null;    // legacy frame, not binframe
   }
   if (dv.getUint8(2) !== RR_BINFRAME_VERSION) {
      console.log("binframe: unsupported version", dv.getUint8(2));
      return null;
   }

   var payload_len = dv.getUint32(16, false);   // big-endian
   if (payload_len + RR_BINFRAME_HDR_LEN > buf.byteLength ||
       payload_len > RR_BINFRAME_MAX_PAYLOAD) {
      console.log("binframe: bad payload_len", payload_len, "buf", buf.byteLength);
      return null;
   }

   // codec magic: 4 ASCII bytes, zero padded
   var codec = "";
   for (var i = 0; i < 4; i++) {
      var c = dv.getUint8(4 + i);
      if (c !== 0) {
         codec += String.fromCharCode(c);
      }
   }

   return {
      subsystem: dv.getUint8(3),
      codec: codec,
      dir: dv.getUint8(8),
      vfo: dv.getUint8(9),
      rig: dv.getUint8(10),
      stream: dv.getUint8(11),
      seq: dv.getUint32(12, false),
      payload_len: payload_len,
      ts: Number(dv.getBigUint64(20, false)),
      payload: new Uint8Array(buf, RR_BINFRAME_HDR_LEN, payload_len)
   };
}

// Convenience: is this an audio payload frame (subsys audio, has payload)?
function binframe_is_audio(f) {
   return f && f.subsystem === RR_BINFRAME_SUBSYS_AUDIO && f.payload_len > 0;
}

function binframe_build_audio(codec, direction, vfo, rig, stream, payload) {
   if (!codec || codec.length !== 4 || !payload || payload.byteLength === 0 ||
       payload.byteLength > RR_BINFRAME_MAX_PAYLOAD) {
      return null;
   }
   var bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
   var buf = new ArrayBuffer(RR_BINFRAME_HDR_LEN + bytes.byteLength);
   var dv = new DataView(buf);
   dv.setUint8(0, RR_BINFRAME_MAGIC0);
   dv.setUint8(1, RR_BINFRAME_MAGIC1);
   dv.setUint8(2, RR_BINFRAME_VERSION);
   dv.setUint8(3, RR_BINFRAME_SUBSYS_AUDIO);
   for (var i = 0; i < 4; i++) dv.setUint8(4 + i, codec.charCodeAt(i));
   dv.setUint8(8, direction);
   dv.setUint8(9, vfo === undefined ? 0 : vfo);
   dv.setUint8(10, rig === undefined ? 0 : rig);
   dv.setUint8(11, stream === undefined ? 0 : stream);
   dv.setUint32(12, 0, false);
   dv.setUint32(16, bytes.byteLength, false);
   dv.setBigUint64(20, BigInt(Math.max(0, Math.floor(performance.now() * 1000))), false);
   new Uint8Array(buf, RR_BINFRAME_HDR_LEN).set(bytes);
   return buf;
}
