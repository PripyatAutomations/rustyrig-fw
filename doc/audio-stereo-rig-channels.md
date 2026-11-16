# Two rigs on one stereo audio interface

RustyRig can use a single stereo PulseAudio/PipeWire hardware interface
as two independent *mono* rig audio interfaces. A practical mapping is:

| Physical channel | Rig | Capture (receiver → server) | Playback (server → transmitter) |
| --- | --- | --- | --- |
| Left / channel 0 | rig0 | stereo mic **L** → 16 kHz mono RX | 16 kHz mono TX → stereo speaker **L** |
| Right / channel 1 | rig1 | stereo mic **R** → 16 kHz mono RX | 16 kHz mono TX → stereo speaker **R** |

See [the example config](../config/examples/stereo-two-rigs.cfg).
Merge the `[rig:*]` and `[pipelines]` sections into the server's active
rig/pipeline configuration. The example uses `device=default` in four
places: change those to the actual **same physical stereo source** for
both `src.*` endpoints and **same physical stereo sink** for both
`sink.*` endpoints. Confirm your interface exposes both channels;
a mono microphone device will not provide the second channel.

## GStreamer routing

The two `src.*` fwdsp workers open the same stereo capture device
through PulseAudio/PipeWire. Each uses `deinterleave` and consumes
exactly one of `split.src_0` or `split.src_1`. The other pad drains
into a `fakesink`, preventing an unlinked stereo pad. Each selected
mono stream then converts to the unchanged 16 kHz/S16LE rig encoder
contract.

For playback, each `sink.*` worker receives its own rig's 16 kHz mono
TX decoder output. `audiopanorama method=simple` performs hard-left
(`-1.0`) or hard-right (`1.0`) panning into a stereo stream. The
PulseAudio/PipeWire sound server mixes the two independently opened
playback streams into one stereo output. The hardware sees left-only
rig0 and right-only rig1; transmitting on one does not intentionally
drive the other channel.

`deinterleave` and `audiopanorama` are provided by GStreamer Good
Plug-ins; see the official
[deinterleave](https://gstreamer.freedesktop.org/documentation/interleave/deinterleave.html)
and [audiopanorama](https://gstreamer.freedesktop.org/documentation/audiofx/audiopanorama.html)
references.

## Limits and future editor integration

- Each rig still owns its own RX/TX endpoint workers and media streams.
  There is **no** combining their 16 kHz network audio, and no
  assumption that their tuners, VFOs, TX/PTT state, or recording should
  share a media channel.
- The current configuration opens two capture clients and two playback
  clients against the same physical device, rather than running one
  dedicated multi-rig device-owning mixer. Actual isolation depends on
  the device being exposed to the sound server as true stereo and the
  simple panner performing hard isolation.
- Validate the sound-card channel order with a loopback test before
  connecting a transmitter. Keep both transmitters disabled until
  channel mapping and PTT routing are confirmed.
- This config is a concrete **pipeline-based option today**, not yet a
  first-class `channel=left|right` field in the GTK routing editor.
  The later automatic pipeline generator should represent hardware
  channel assignments explicitly and create one shared device node
  with separate per-rig logical capture/playback ports. That would
  avoid duplicated hardware source streams and make channel isolation
  visually inspectable.

Note that this is **server-side rig hardware wiring**. The client
Routing tab's mono source with separate `L OUT`/`R OUT` controls
instead represents the *listener's ear routing*. They are different
levels of the audio graph.

## Client Routing aliases (selected rig)

The client's Routing view also exposes the selected radio as two stable
logical ports:

- `rig.src` — decoded *receive* audio **from** the selected radio.
  This is an alias of its existing RX stream, not a second decoder or
  an extra copy in the mixer.
- `rig.sink` — *transmit* audio **into** the currently selected radio.
  This only appears when a matching TX media channel is advertised.
  Its microphone link uses the existing codec/PTT-checked encoder path;
  a graph cable does **not** grant PTT permission.

Other subscribed rigs keep their independent RX stream IDs and TX sink
identities. The active aliases change their media backing when the user
changes the selected rig/VFO, while their graph positions stay anchored
to `rig.src` and `rig.sink`.

### Multi-application routing direction

A future virtual PulseAudio/PipeWire **sink** accepts playback from a
local application, then feeds a rig TX mix input. A virtual **source**
exposes the rig's RX audio for the application's recording input.
For example, `ttyCAT0`, `app0.playback` and `app0.capture` would
belong to one independently mapped VFO/radio binding, without making
that binding follow the GUI's current selected radio.

**Not implemented yet:** independent multi-input TX mixing, per-program
PulseAudio/PipeWire devices, multiple simultaneous encoded TX media
destinations, or CAT/PTT arbitration for several clients. Those need
stable per-rig resource bindings, explicit audio patch tables and a
PTT-authorized TX mixer. A visual graph edge must not claim PCM delivery
until its actual DSP/transport route exists.
