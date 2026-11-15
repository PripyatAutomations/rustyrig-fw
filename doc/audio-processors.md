# Audio processor groundwork

`fwdsp -T` selects a generic framed PCM pipeline, separate from codec encoder
and decoder modes. `-P <name>` selects the configured pipeline
(`pipeline:proc.<name>`); with an inline `-p` pipeline, `-P` is optional. Named
source, sink, and recoder pipelines use `pipeline:src.<name>`,
`pipeline:sink.<name>`, and `pipeline:recode.<name>`, respectively.
The pipeline may be bidirectional for effects, source-only to capture host audio,
or sink-only to play framed PCM to a host audio device. The optional `-M`
argument lets the manager require the matching endpoint shape and fail quickly
if a configured pipeline does not provide it.

The manager exposes `fwdsp_processor_start()` for bidirectional `proc.*` effects,
`fwdsp_audio_capture_start()` for a `src.*` endpoint, and
`fwdsp_audio_playback_start()` for a `sink.*` endpoint. Output is delivered through a
callback on the manager/event-loop thread, so callbacks must return promptly.
The server's `src.rig0` and `sink.rig0` pipelines use PulseAudio (including
PipeWire's PulseAudio compatibility layer) by default and can be overridden in
the normal `[pipelines]` config section. Set `device=` in the pipelines when
the rig audio interface is not the host default. Each `[rig:<alias>]` section
can select separate `audio.source=src.<name>` and `audio.sink=sink.<name>`
endpoints. Set either to `none` to disable that direction completely, even
when a built-in default pipeline exists. No fwdsp subprocess starts for
a disabled endpoint; the other direction remains independently available.

For a host with PipeWire's PulseAudio compatibility layer or PulseAudio,
run `sh tools/ensure-dummy-audio.sh` as the audio session user to create a
`rustyrig_test` null sink and `rustyrig_test.monitor` recording source
if absent. This is idempotent for a running PulseAudio-compatible service;
the device may need recreation when that service restarts. Example capture:
`pulsesrc device=rustyrig_test.monitor`; example playback:
`pulsesink device=rustyrig_test`. A null monitor emits silence until
something plays into the sink. The server needs access to the same
PulseAudio-compatible session as the script; system services do not
automatically see a desktop user's audio server. Normal codec IDs always use
the `src.rig0` host/radio input. With `audio.test-mode=true`, the server also
advertises RX-only tone (`*T`) and pink-noise (`*P`) codec variants, whose
fwdsp pipelines generate their own source. Set it to `false` to hide those
test variants. A radio TX channel always uses a normal codec and the client's
microphone. The `src.pink` and `src.tone660` endpoints remain available for
explicit processor tests.
The selected rig RX source is framed into each active VFO RX encoder. Incoming
talker packets are sent to other subscribers on that VFO, excluding the origin,
decoded to canonical PCM, and written to `sink.rig0`. The decoder's optional
`hub-sink` branch is enabled for server codec processes by `[fwdsp] pcm-hub=true`;
it emits a distinct framed PCM tap to the manager. `proc.*` remains reserved
for effects/transforms, while `recode.*` is reserved for codec recoders.

Every IPC side carries mono S16LE PCM at 16 kHz in the existing `fwdsp` frame
format: a four-byte big-endian byte count, followed by sample bytes. Input and
output GStreamer endpoints use `appsrc name=processor-src` and
`appsink name=processor-sink`, respectively. The fwdsp child validates the
caps and rejects non-PCM output.

This is the first server-side PCM hub path. The audio endpoints can now connect
the host sound interface to the hub; selectable test sources remain available.
Per-rig/per-VFO effect chains and hardware backend integration remain follow-up
work. The shared media channel codec negotiation remains in force, so each VFO
currently has one active encoder format for its listeners.

## Audio Routing graph and per-pin gains

Audio Routing now opens as a **full-size main-content tool page**, rather
than a floating window. The top toolbar's Audio Routing button toggles
that page and returns specifically to the Status tab. Its page is hidden
from the main tab strip, as with the Admin and Config tools. The internal
**Routing** and **Pipelines** tabs remain available. The floating title
bar, close/minimize/resize buttons, and floating-panel zoom controls are
not used here; use the existing mouse wheel, touchpad or keyboard zoom
commands and canvas panning instead.


Source output sockets are filled circles; input sockets are hollow,
heavy outlined rings. Connections terminate in arrows pointing into
their receiving sockets. A dragged connection follows the pointer until
near a compatible input, where the preview snaps to its center. Dropping
there connects it. Right/left ear gains are independent of source PCM
channel count: a 16 kHz mono rig can send the same signal to either or
both speaker ears, and therefore has two labeled **mixer-destination**
ports and trims even though it has only one underlying source channel.

For canvas navigation, **mouse wheel zooms under the pointer**, smooth
touchpad scrolls pan, **Shift+wheel** pans horizontally, and
**middle-button drag** or **Alt+left drag** pans freely. These operations
affect only the view and never modify live audio routing.

To share one stereo hardware sound card between two rigs with separate
left/right physical inputs and outputs, see
[two rigs on one stereo audio interface](audio-stereo-rig-channels.md)
and the [reference pipeline config](../config/examples/stereo-two-rigs.cfg).


Routing blocks can be dragged by their body or title without touching their
audio connections. Positions are stored in the rrclient settings database,
keyed by a SHA-256 of each stable audio node ID; restarting the client or
disconnecting a receiver does not lose manually placed coordinates.
Port dragging and L/R trim sliders retain their own gestures. **Auto Layout**
clears manually saved positions and arranges sources, the 16 kHz radio bus,
the resampling bridge, HQ bus and sink in centered left-to-right columns.
Isolated microphone capture occupies a separate lane. Audio route presets
remain independent of the visual layout.


The GTK **Audio Routing** panel uses a distinct network/graph toolbar icon,
leaving the speaker/volume icon to the Volume widget. Its L/R trims live
inside each block next to the corresponding audio port, not on the wires.
The source output trim uses the existing per-source input gain at the PCM
mixer. The mixer's L/R output trims and the playback sink's L/R input trims
have separate gain stages in the client PCM engine (all -60 to +12 dB,
0 dB unity). Each stage persists its values in the client database and
updates through the existing GTK-to-core audio-control event path. Recording
and analysis taps remain before listening trims. The sink block is presented
as a GStreamer pipeline container; clicking it opens the existing isolated
pipeline inspector.

The embedded pipeline editor uses the shared graph renderer and the
`rrpipelines` model. Its GStreamer plugin registry runs in rrclient's GTK
process for editing only; fwdsp remains the pipeline execution boundary.
Live runtime topology inspection and safe hot-editing require a versioned
fwdsp snapshot and validated worker control transactions. Never confuse
configured launch strings with the currently executing DSP pipeline.

## Opt-in generated client sound-device pipelines

Existing `pipeline:src.client.dsp0` and `pipeline:sink.client.mix` configuration
values remain the **default** and continue to be supported. To test the
shared recipe compiler instead, set `audio.pipeline-mode=generated` in
rrclient's configuration. Set `audio.capture-device` and
`audio.playback-device` to PipeWire-Pulse/PulseAudio device names, or leave
both as `default`. The existing `audio.capture-rate` and
`audio.playback-rate` settings still control the requested hardware rates.

The recipe compiler produces the same canonical fwdsp capture/playback
endpoints and runs through the existing subprocess manager; the client
never loads or processes GStreamer audio directly. Invalid recipes fall back
to the configured launch string. This is an initial, opt-in endpoint generator,
**not** a distributed graph compiler or live LV2 effect host yet. The
configured pipeline path remains an indefinitely supported advanced option.

## Guided client devices and compact controls

Config → **Audio Devices** provides a separate capture/playback device and rate
form, an opt-in **Generated** mode, and a preview of what will run inside
`fwdsp`. Apply Settings stages config changes; use **Save Config** and restart
the client to start them. The default **Configured pipelines** mode continues
using the existing `pipeline:src.client.dsp0` and
`pipeline:sink.client.mix` launch strings.

Routing → **Add Control…** creates a compact dockable panel that may
start unbound. It can be wired by dragging its blue socket onto an existing
gain pin. The **Add Effect…** chooser lists GStreamer-discovered LV2 plugins;
selecting an LV2 block in the Pipelines editor and clicking
**LV2 Controls…** lists its plugin-owned numeric, boolean and enum control
properties with names, types and ranges. These are metadata inspection
pins, **not yet runtime-connectable LV2 ports**: mapping a blue control wire
to an effect parameter requires a parameter identity and control IPC across
the fwdsp worker boundary.

The floating controls can also be bound to an
existing input/output gain pin. Select its stable audio node, left/right
channel, short label and presentation: horizontal fader, vertical fader or
rotary knob. All three send the same asynchronous gain-control command as
the routing canvas and double-click resets to 0 dB. The floating-panel
framework preserves window position and supports docking/roll-up.
Disconnected targets are disabled until their stream returns. Parameter
bindings currently cover the existing mixer gains; LV2 parameter port
binding, saving/reloading control definitions, and placement on other
cluster nodes remain subsequent steps.

## Client sound-device rate selection

The stock rrclient microphone and stereo playback GStreamer pipelines request
48 kHz in `auto` mode, or the separately configured 44.1/48 kHz device rate.
This rate is at the physical PulseAudio/PipeWire boundary; the current framed
PCM hub stays at 16 kHz S16LE. The Audio Routing window shows the requested
rate and the separate TX microphone endpoint, without suggesting the capture
endpoint is an RX mixer input. Custom pipelines remain manually configurable.

See [audio sample-rate domains](audio-rate-domains.md) and the
[editable diagram](audio-rate-domains.svg) for implemented signal paths and
the future per-rate bus / automatic GStreamer pipeline design.

## Embedded Audio Routing / Pipelines workspace

The GTK Audio Routing floating workspace contains **Routing** and
**Pipelines** tabs. Routing provides the per-ear graph, gain controls and
presets; Pipelines embeds the existing GStreamer graph editor directly into
rrclient's GTK module (rather than launching another GUI process). The
editor's GStreamer element model, property controls, serialized pipeline
validation, and application through the Config layer are preserved.

Double-click a pipeline-backed Routing block to focus its configured
GStreamer definition. Double-click a GStreamer element in a mapped pipeline
to return to its owning Routing block. Unmapped elements do not create
fictitious audio routes.

The in-client editor is separate from the external **fwdsp worker process**
which still executes audio pipelines. Config changes do not silently hot
replace a live pipeline. Later work will add a versioned worker topology
snapshot and transactional live-edit control protocol. The eventual
automatic AF pipeline builder must follow the same separation of concerns.

The Routing view now exposes lock-protected source PCM metadata from the
**actual** running client mixer: source rate, S16LE/F32LE, mono/stereo,
whether the producer-side FIR is resampling into the mix clock, and
per-source buffered milliseconds (`Q`), overflow-dropped frames (`D`),
and starvation/rebuffer events (`U`). Hover a node for these details.
Device capture/playback rates are configuration *requests*, not actual
negotiated hardware caps; they are labeled as such. The live Routing graph
now includes the 16 kHz `rx.mix` bus, its real producer-side
`rx16.resample` converter, and the independent 44.1/48 kHz `hq.mix`
output bus. HQ-local sources enter `hq.mix` directly. Both mixer workers
report independent scheduling and buffering diagnostics. Legacy recording
and AF analysis remain tapped from the 16 kHz bus; full-HQ recording is a
separate follow-up.

