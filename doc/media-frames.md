# RustyRig binary media frame specification (binframe v2)

This document specifies the payload format of `WEBSOCKET_OP_BINARY`
websocket frames used for media (audio, video, waterfall, modem data,
file transfer, and media control). It is implemented by:

- `librrprotocol/ws.binframe.h` and `librrprotocol/binframe.c` (authoritative)
- C clients/servers via `ws_binframe_process*()` in `librrprotocol/cli.main.c`
  and `librrprotocol/srv.http.c`
- Browser client via `rustyrig-www/js/webui.audio.framing.js` (mirrored)

Use `grep -R 'PARITY:'` to find mirrored implementations.

## Goals

1. One binary frame format for all media-ish subsystems, so a receiver
   can tell audio from modem data from file chunks by inspecting the
   frame alone (we currently mix file-xfer and audio on the same
   websocket with no discriminator).
2. Associate every frame with its rig, VFO and logical stream so
   multiple rigs/VFOs can be streamed on one websocket connection.
3. Allow a receiver that has not (yet) negotiated to still route or
   discard frames cheaply.
4. Cheap to parse in C and JavaScript, and cheap to produce from a
   gstreamer pipeline (the fwdsp child processes see header + payload).

## Frame layout

All multi-byte fields are big-endian (network byte order). The header
is 28 bytes, packed, followed by `payload_len` bytes of payload.

    offset  size  field
    ------------------------
    0       2     magic = 'R','R' (0x52 0x52)
    2       1     version (0x01)
    3       1     subsystem (see below)
    4       4     codec magic (ASCII, e.g. "pc16"; zeroes if none)
    8       1     direction (0 = RX, 1 = TX)
    9       1     vfo (0 = VFO A, 1 = VFO B, ... 0xFF = not applicable)
    10      1     rig (rig index; 0 = the one and only rig; 0xFF = n/a)
    11      1     stream (per-connection stream id assigned by the sender;
                          0 = default/only stream)
    12      4     seq (uint32 sequence number, wraps; 0 = first frame)
    16      4     payload_len (uint32; see limits below)
    20      8     ts (uint64, microseconds since the UNIX epoch; 0 = unset)
    28      ...   payload

In C:

    struct rr_binframe_hdr {                 /* exactly 28 bytes */
       uint8_t  magic[2];                    /* 'R','R' */
       uint8_t  version;                     /* RR_BINFRAME_VERSION */
       uint8_t  subsystem;                   /* RR_BINFRAME_SUBSYS_* */
       char     codec[4];                    /* codec magic, e.g. "mu16" */
       uint8_t  direction;                   /* RR_BINFRAME_DIR_* */
       uint8_t  vfo;
       uint8_t  rig;
       uint8_t  stream;
       uint32_t seq;
       uint32_t payload_len;
       uint64_t ts;
    } __attribute__((packed));

All integers are stored big-endian on the wire; use `hton*/ntoh*` (or
`DataView` with `littleEndian = false` in JS) rather than reading the
struct directly, to stay correct on either-endian hosts.

### Subsystems

| value | name                | notes |
|-------|---------------------|-------|
| 0x00  | none                | invalid on the wire |
| 0x01  | audio               | codec required |
| 0x02  | video               | codec required |
| 0x03  | waterfall/spectrum  | codec optional |
| 0x04  | modem               | codec optional |
| 0x05  | file transfer       | legacy ws.file-xfer.c moves to this |
| 0x06  | control             | in-band media control (future) |
| 0xFF  | keepalive           | no payload required |

### Direction

Direction is from the *sender's* perspective, matching the
`media.codec` negotiation channel names:

| value | meaning |
|-------|---------|
| 0x00  | RX (received media being forwarded, i.e. rig -> listener) |
| 0x01  | TX (captured media being sent toward the rig) |
| 0xFF  | not applicable |

## Limits

- `payload_len` must be `<= 65535 - 28` so the whole frame fits the
  existing `HTTP_WS_MAX_MSG` websocket message limit.
- Frames with `payload_len` exceeding that limit MUST be dropped by
  receivers (log at LOG_DEBUG).
- Receivers MUST validate `magic` and `version` and MUST validate
  `payload_len` against the actual buffer length. Mismatches are
  dropped, never fatal.

## Compatibility

Legacy frames (the old 4-byte `chan/seq` audio framing produced by
`www/js/webui.audio.framing.js`, the 24-byte file-xfer header, and the
bare codec-prefixed audio the server used to send) do not start with
`'RR'`, so parsers fall back to legacy handling until all parties are
upgraded, then may drop the legacy paths.

## Relationship to codec negotiation

Codecs are negotiated out-of-band with JSON text frames before any
media flows (see below). The `codec` field in each frame carries the
4-byte codec magic actually used for that stream's payload, so a
receiver joining mid-stream (or a sniffer) can decode without extra
state. The `stream` id lets a connection carry more than one codec or
more than one rig's audio simultaneously.

## Codec negotiation (media.* JSON text frames)

Carried on the same websocket as text frames using the existing
`msg.type` = `media` envelope. All frames are single JSON objects.

1. Server -> client after successful auth (`media.capab`):

       { "msg": { "type": "media" },
         "media": { "cmd": "capab", "codecs": "mu16 pc16 mu08" } }

   `codecs` is the server's `codecs.allowed` list, space separated, in
   preference order.

2. Client -> server, its own capability list (`media.capab`):

       { "msg": { "type": "media" },
         "media": { "cmd": "capab", "codecs": "pc16 mu16 mu08" } }

3. Server -> client, intersection and default (`media.isupport`):

       { "msg": { "type": "media" },
         "media": { "cmd": "isupport",
                    "codecs": "mu16 pc16",
                    "preferred": "mu16", "ts": 1739560000 } }

4. Client -> server, per-direction selection (`media.codec`):

       { "msg": { "type": "media" },
         "media": { "cmd": "codec",
                    "codec": "mu16", "channel": "tx" } }

   `channel` is one of `tx`, `rx`, `video-tx`, `video-rx`. On receiving
   this, the server stores `cptr->codec_tx`/`cptr->codec_rx`, spawns or
   reuses the matching fwdsp gstreamer child, and starts routing
   binary frames for that direction.

Both directions must complete steps 1-3 before any audio binary frames
are considered valid; the server drops audio frames for a connection
with no negotiated codec for that direction.

## gst/fwdsp wiring

Each fwdsp subprocess reads raw frames on stdin and writes raw frames
on stdout. In the direction of server -> fwdsp, the payload written is
the 28-byte header followed by the codec payload as received over the
websocket. In the direction fwdsp -> server, the subprocess writes the
same header with `direction` set appropriately and `codec` filled in;
the server strips the header, rewrites `rig`/`vfo`/`stream`/`seq`
central values, and fans the payload out to subscribed websocket
clients as a normal binframe. The `stream` id assigned by the server
matches the `fwdsp_subproc.chan_id`.
