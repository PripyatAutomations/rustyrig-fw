//
// inc/librustyaxe/ws.binframe.h: Websocket binary frame wrapping
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

//
// Specification: doc/media-frames.md (binframe v2)
//
// PARITY: rustyrig-www/js/webui.audio.framing.js
// PARITY: rustyrig-www/js/webui.js (handle_binary_frame)

#if     !defined(_ws_binframe_h)
#define	_ws_binframe_h
#include <librustyaxe/config.h>
#include <librustyaxe/logger.h>
#include <stdint.h>
#include <stddef.h>

#define	RR_BINFRAME_VERSION	0x01

// Subsystem byte (hdr.subsystem)
#define	RR_BINFRAME_SUBSYS_NONE		0x00
#define	RR_BINFRAME_SUBSYS_AUDIO	0x01
#define	RR_BINFRAME_SUBSYS_VIDEO	0x02
#define	RR_BINFRAME_SUBSYS_WATERFALL	0x03
#define	RR_BINFRAME_SUBSYS_MODEM	0x04
#define	RR_BINFRAME_SUBSYS_FILE		0x05
#define	RR_BINFRAME_SUBSYS_CONTROL	0x06
#define	RR_BINFRAME_SUBSYS_LOG		0x07
#define	RR_BINFRAME_SUBSYS_KEEPALIVE	0xFF

// Direction byte (hdr.direction), sender's perspective
#define	RR_BINFRAME_DIR_RX		0x00
#define	RR_BINFRAME_DIR_TX		0x01
#define	RR_BINFRAME_DIR_NA		0xFF

#define	RR_BINFRAME_VFO_NA		0xFF
#define	RR_BINFRAME_RIG_NA		0xFF
#define	RR_BINFRAME_STREAM_NONE		0x00

#define	RR_BINFRAME_HDR_LEN		28
// Frames must fit within the websocket max message limit
#define	RR_BINFRAME_MAX_PAYLOAD		(HTTP_WS_MAX_MSG - RR_BINFRAME_HDR_LEN)

struct rr_binframe_hdr {
   uint8_t  magic[2];                    // 'R','R'
   uint8_t  version;                     // RR_BINFRAME_VERSION
   uint8_t  subsystem;                   // RR_BINFRAME_SUBSYS_*
   char     codec[4];                    // codec magic, e.g. "mu16" (no NUL)
   uint8_t  direction;                   // RR_BINFRAME_DIR_*
   uint8_t  vfo;                         // 0 = VFO A ... 0xFF = n/a
   uint8_t  rig;                         // rig index; 0 = default; 0xFF = n/a
   uint8_t  stream;                      // sender-assigned stream id
   uint32_t seq;                         // wraps; big-endian on the wire
   uint32_t payload_len;                 // big-endian on the wire
   uint64_t ts;                          // usec since epoch; 0 = unset
} __attribute__((packed));

// Parsed view of a frame; data points into the caller's buffer
struct rr_binframe {
   struct rr_binframe_hdr hdr;
   const uint8_t *data;                  // payload (not NUL terminated)
   size_t len;                           // payload length
};

// Prepare a header for wire transmission: fills magic/version and
// converts all fields to wire order in out (out is
// RR_BINFRAME_HDR_LEN bytes). Returns the header length or -1 on
// invalid args.
extern int rr_binframe_pack_hdr(uint8_t *out, size_t outlen, uint8_t subsystem,
   const char codec[4], uint8_t direction, uint8_t vfo, uint8_t rig,
   uint8_t stream, uint32_t seq, uint32_t payload_len, uint64_t ts);

// Parse and validate a frame from buf. Returns 0 and fills f on
// success (f->data points into buf), -1 on invalid/unparseable data.
extern int rr_binframe_parse(const uint8_t *buf, size_t len, struct rr_binframe *f);

// Convenience: pack a complete frame (header + payload) into a
// malloc'd buffer returned via *out. Returns total length or -1.
extern int rr_binframe_frame(uint8_t **out, uint8_t subsystem, const char codec[4],
   uint8_t direction, uint8_t vfo, uint8_t rig, uint8_t stream, uint32_t seq,
   uint64_t ts, const void *payload, size_t payload_len);

// Hand a validated frame to the appropriate subsystem handler.
// Emits event_emit_binary("media.frame.<subsystem>", cptr, data, len).
// Returns false if handled, true if dropped/unhandled.
extern bool rr_binframe_dispatch(struct rr_binframe *f, void *ctx);

// Host log line framing (SUBSYS_LOG payload). The log subsystem (ts,
// priority, subsys) rides in a fixed NUL-padded header so the payload is
// an unmangled NUL-terminated log line; see rrserver/hostlog.c.
// PARITY: rrclient/gtk.syslog.c host_log_frame_handler()
struct rr_logframe {
   uint8_t  prio;                        // logpriority_t cast to uint8_t
   char     subsys[16];                  // NUL padded log subsystem
   // payload: the log message, NUL terminated (no trailing newline)
};
#define	RR_LOGFRAME_HDR_LEN	17        // 1 + 16

// Pack a log line into a SUBSYS_LOG binframe (malloc'd, returned via
// *out; total frame length returned, or -1). seq/ts are filled by the
// caller (or 0/0 to let the transport decide).
extern int rr_logframe_frame(uint8_t **out, logpriority_t priority,
   const char *subsys, const char *msg, size_t msg_len,
   uint32_t seq, uint64_t ts);

#endif // !defined(_ws_binframe_h)
