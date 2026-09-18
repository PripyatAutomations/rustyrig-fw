# Native audio codec selection

The GTK RX/TX pickers and the shared GTK/TUI commands use the codec list
negotiated with the server. The supplied configuration and built-in defaults
provide these mono codecs:

| ID | Format | Sample rate | Payload bitrate |
|---|---|---|---|
| `pc16` | Signed 16-bit PCM | 16 kHz | 256 kbit/s |
| `g722` | G.722 wideband ADPCM | 16 kHz | 64 kbit/s |
| `mu16` | G.711 mu-law at 16 kHz | 16 kHz | 128 kbit/s |
| `mu08` | G.711 mu-law | 8 kHz | 64 kbit/s |
| `opus` | Opus | 16 kHz input | Configured by the encoder |

G.722 uses GStreamer's `avenc_g722` and `avdec_g722` from gst-libav.
Mu-law uses `mulawenc` and `mulawdec`. Both endpoints need the plugins
for the selected codec; negotiation lists configured codecs, not an inventory
of installed plugins. G.722 retains more audio bandwidth than `mu08` at the
same payload bitrate. `pc16` is the uncompressed fallback.

## Commands

These commands work in both native client views:

```text
/rxcodec
/txcodec LIST
/rxcodec g722
/txcodec mu16 #2
/rxcodec mu08 <channel-uuid>
/rxcodec NONE
```

With no arguments, or with `LIST`, the command lists negotiated codec choices
and the direction's subscribed or explicitly disabled audio channels. Channel
numbers match `/media LIST`; a bare number or `#number` may be used.

Setting a codec without a channel targets all subscribed audio channels in
that direction, including channels previously disabled with `NONE`. An optional
UUID or channel number targets one channel. Use `/media SUBSCRIBE` to select
other available channels first. Codec names are case-insensitive.

`NONE` unsubscribes the affected audio channels and releases their local audio
pipeline. Decoders stop immediately; unused encoders pause and retain the
existing `fwdsp.hangtime` lifetime. `NONE` is local subscription intent, not a
codec sent to the server. Selecting a codec again re-subscribes disabled
channels after the server confirms that codec. A direction disabled with
`NONE` stays off through channel announcements until re-enabled or disconnected.

The GTK pickers apply to all subscribed channels in their direction. The native
audio bridge still has one local pipeline per direction; the displayed codec
prefers a subscribed channel on the active VFO. These controls do not add
simultaneous decoding or mixing of multiple independent RX streams.

## Pipeline configuration

The `[pipelines]` sections in `config/rrclient.cfg` and `config/rrserver.cfg`
contain `<codec>.rx` and `<codec>.tx` entries (internally stored as
`pipeline:<codec>.rx` and `pipeline:<codec>.tx`). Shared built-in
defaults live in `fwdsp/default-pipelines.h`, used by the client, server and
fwdsp. Keep those defaults and the supplied configurations synchronized.
`codecs.allowed` controls advertisement; client `audio.prefer-codecs` controls
preference. Restart after editing configuration.

The supplied TX pipelines retain the existing `audiotestsrc` test tone.
Replace that source with the station's capture source for live audio.
Each pipeline keeps its raw S16LE `record-sink` branch for FLAC recording;
encoded transport packets remain length-framed.

Headless validation: `bash tests/fwdsp/test_codec_roundtrip.sh` checks all five
codecs in both configurations and the built-in defaults, including fragmented
and coalesced transport writes. `bash tests/rrclient/test_codec_commands.sh`
checks command selection, channel targeting, NONE and re-enabling.
These checks do not establish that live Opus playback works on a particular
sound device.
