# Network-connected SDR nodes

**Status: design proposal — not yet an implemented feature.**

RustyRig should eventually let an SDR daemon run next to its hardware, connect outward to an rrserver link port, and offer radio resources to operators anywhere on the network. The same model must support RX-only devices and TX-capable SDRs.

## The idea

- **SDR node (`rrsdrd`):** Talks to one or more physical SDRs, advertises hardware capabilities, and handles high-rate IQ and DSP locally.
- **rrserver:** Assigns slices, coordinates operators and permissions, decides when hardware may retune, and distributes media to subscribers.
- **rrclient:** Presents VFO controls and subscribes to audio or spectrum streams. Each VFO has a stable UUID; a room does not have to represent a physical radio.

Several listeners should share one demodulated slice where possible. Clients normally receive narrowband audio, **not** a copy of the full IQ stream. Each node may offer several independent devices or RF frontends.

## Sharing spectrum without interrupting users

A physical receiver captures a limited usable RF bandwidth around its center frequency. Several logical slices can coexist inside it. For example, a device with sufficient usable bandwidth might place requests for 5.1 and 5.3 MHz around a 5.2 MHz center.

The node reports which placements are physically possible, including sampling limits, filter guard bands, and shared hardware constraints. rrserver decides whether a proposed change is allowed given active users and their permissions.

A center-frequency move should be evaluated before applying it. Existing protected slices must remain covered. Otherwise rrserver can select another frontend, request permission to preempt, or explain why the request cannot be satisfied. Failed changes must not silently disconnect listeners.

## RX and TX are capabilities, not different architectures

Devices and slices advertise their actual RX/TX capabilities: receive-only, transmit-only, half-duplex or full-duplex. A TX-capable SDR still needs explicit authorization to transmit.

The node must independently enforce hardware limits and interlocks, approved frequencies and power, watchdogs, maximum key time, and **unkey on disconnect or expired authorization**. rrserver manages short-lived, exclusive TX leases and audits transmission. UI controls and room membership alone are not permission to transmit. Shared LO, antenna and duplex constraints must be respected during arbitration.

## Resource and protocol model

Keep distinct identities for the node, physical device, independently tunable RF frontend, shared wideband capture, logical slice, VFO, subscriber and control lease.

The link protocol will need authenticated node registration, device inventory, capability changes, slice requests, feasibility checks, center-frequency prepare/commit/abort, stream subscriptions, heartbeats, and resource/lease recovery after reconnect. Use versioned messages and generation numbers so stale requests cannot change hardware state.

Transport remains replaceable through librrprotocol; link control, audio and optional decimated waterfall/spectrum updates have different bandwidth and reliability needs. Do not tie the architecture to one transport.

## First-class SDR tuner and slice VFO resources

The primary goal is a single RustyRig control surface for homebrew,
commercial and network-attached radios, including SDR hardware. A physical
SDR frontend and its RF slices are distinct catalog resources:

- **Physical tuner / capture**: one hardware LO and sample clock, with
  center RF frequency, usable bandwidth, antenna, gain, rate and device
  limits. It owns hardware retuning, not any individual slice.
- **RF slice / derived VFO**: a stable VFO identity with its own *tunable
  digital center*, mode, filter bandwidth, demodulator, stream subscriptions
  and grants. Moving a slice VFO changes NCO/channelizer settings **within
  the parent capture**, never its hardware LO.
- **Capture protection**: a requested parent-LO retune needs feasibility
  and permission checks for *all* affected slices. No listener may silently
  move or lose its slice when another operator tunes a VFO.
- **Signal path**: hardware I/Q -> shared capture -> digital tuner/filter/
  decimator -> demodulator -> individually selected-rate audio -> one or more
  client speakers/recorders or digital-mode consumers. IQ subscribers may
  consume the slice *before* demodulation without losing phase information.

For the first RTL-SDR bridge, expose a parent tunable SDR control resource
and multiple independently digitally tunable child VFOs. The latter can
slide anywhere whose *entire filter passband* fits inside the usable parent
capture; they cannot change the RTL-SDR's hardware center. Parent tuning
should require ownership/authorization and explicit handling of subscribers.
Multiple listeners of an identical slice should share one channelizer and
decoder; distinct slice tuning should not unnecessarily consume separate
hardware resources.

The existing `rrserver/rx.slice` registry handles deduplicated identities
and subscriptions but does not yet implement channelization. The
`librrsdrutils` descriptors support source/slice bandwidth validation and
`rr_slice_can_tune()` for digital center changes. An RF slice's center
frequency should be mutable VFO state with a persistent ID; don't make
a new identity for every change or treat a derived VFO as non-tunable.
Legacy named slice specifications should eventually be distinguished from
stable logical VFO identity when wiring them into the resource catalog.

## Audio sample-rate contracts

Do not assume every SDR or digital modem emits 16 kHz mono. Each stream
must advertise its PCM sample rate, channel layout, sample format, and clock
domain. Consumers subscribe to a *format*, negotiating compatible rates
rather than interpreting bytes at an implicit fixed rate.

The `rr_pcm_stream_format_t` contract validates rates from 8–192 kHz,
mono/stereo layouts, and signed 16-bit/32-bit-float PCM. The isolated client
mixer now supports **16, 44.1, and 48 kHz output clocks** through
`rr_audio_engine_new_rate(...)` (320, 882, or 960 stereo frames per 20 ms).
The legacy constructor remains 16 kHz, preserving existing ham audio.

`rr_audio_engine_add_stream_format()` registers the exact input clock,
mono/stereo layout, and sample representation. `rr_audio_engine_push_pcm()`
accepts interleaved source frames; a **32-tap, phase-interpolated,
bandlimited FIR** converts each stream on the producer thread into the
mixer clock. Native-rate inputs bypass resampling. Stereo channels remain
independent; mono sources feed both ears. The FIR uses a lower cutoff when
downsampling to suppress out-of-band aliases, preserves conversion phase
across differently sized source chunks, and drains the tail on EOF. Input
format mismatches fail closed. All stream queues retain an 80 ms startup
buffer and 240 ms maximum at the destination clock. The mixer thread has
no resampling, memory allocations, GTK, or GStreamer operations.

`audio_local_rx_start_format()` and `audio_local_rx_push_pcm()` expose this
path to future **local** stereo FM demodulators and hardware audio sources.
The original `audio_local_rx_start()/push()` entry points continue to
expect the legacy native-mixer mono S16 samples. Non-16k/stereo samples
are **not** fed to the existing 16k mono AF spectrum and modem workers.

**Sample rates are internal PCM stream properties, not listening codecs.**
They must not appear in the normal user RX/TX codec selectors.
`fwdsp_audio_resampler_start_to()` still offers GStreamer-based
resampling for mono workers when a standalone DSP bridge is desired.

**Important current limit:** the production client still constructs
a **16 kHz** mixer and launches a 16 kHz sink. Thus high-rate local
sources can be safely mixed/downsampled today, but full-bandwidth 44.1/48
kHz *audible* stereo broadcast playback is **not yet active**. To finish
that migration, switch the sound endpoint, recording encoder and pre-gain
tap, and AF analysis clocks together; then enable the 48 kHz engine in
the client. Extend remote PCM transport with explicit rate/channel
metadata without changing existing negotiated ham listening codecs.
Never silently label 16 kHz audio as 48 kHz or vice versa.

## IQ transport: compression without changing samples

An SDR slice can distribute complex I/Q samples (e.g. CU8, CS16, CF32),
not just demodulated audio. Preserve the full sample clock and absolute
RF center, sample rate, format, channel count, first-sample index,
discontinuities and stream generation. These details are essential for
phase-coherent DSP and future digital decoders.

The initial `rr_iq_chunk_desc_t` validation contract bounds chunk sizes
to 4 MiB, checks exact uncompressed byte counts and supports explicit
`RAW`, `LZ4`, `ZSTD` and reversible integer-delta-plus-ZSTD codec IDs.
**These codec IDs do not yet implement compression or wire encoding.**
Compressed frames must carry exact raw length; unknown codecs fail closed.
Prefer small chunks and low-latency lossless LZ4 for live transport; use
optional Zstd where CPU and latency budgets allow. If compression increases
the size, send RAW. Integer delta coding must be reversible and must not
change float bit patterns. Never compress across a discontinuity without
resetting predictor state. A missing sequence must not be concealed by
fabricating samples: digital consumers need an explicit gap.

Compressing high-entropy RF noise may give little or no savings. The
important bandwidth optimization is to **digitally mix, filter, and
decimate at the SDR node first**, so only the requested bandwidth is
transported. Never decimate/quantize and then call the result lossless
relative to the original input. Media transport and control messages should
remain separate, independently bounded and authenticated.

## GStreamer pipeline composition and visualization

The existing `rrpipelines` editor shares GTK graph drawing primitives with
the client audio-routing graph. Its pipeline canvas now uses **top-down
source-to-sink flow**: GStreamer pad links determine graph layers, branches
are laid out horizontally, and wires run from each element's bottom output
pin to the next element's top input pin. Drag an output pin onto an input
pin to connect elements, or use the existing From/To controls. The
**Arrange top-down** button recomputes the layout; blocks may still be
positioned manually and the canvas expands to fit longer graphs.

These are **editor and presentation improvements**, not runtime graph
reconfiguration yet. `fwdsp` already hosts multiple GStreamer elements in
one subprocess. The next runtime composer should take typed port/caps
descriptions, share capture/tee/filter pipelines within one fault-isolation
domain, and start additional processes only for different hardware,
permissions, failures, or workload isolation. It must validate caps and
queue bounds, apply graph updates transactionally on the owner thread, and
publish actual pad links and runtime status back to the same graph model.
Do not spawn a separate process for every RF slice or DSP element.

## Suggested implementation order

1. Define device, frontend, slice, VFO and lease data types, and document capabilities.
2. Implement authenticated node registration, inventory, health, and resource discovery.
3. Add shared RX capture, feasible slice placement, cooperative retuning, and audio fan-out.
4. Extend multiple-device scheduling, spectrum subscriptions, limits and diagnostics.
5. Add TX operation with explicit leases, node-side failsafes, interlocks and audit logs.
6. Extend resource discovery across rrserver links/mesh later, without changing slice identities.

The underlying data model should permit TX from day one even if receive functionality is implemented first.

## Acceptance tests

Two compatible requests should share one capture; incompatible requests must not disrupt existing listeners. Several subscribers should reuse the same DSP/encoder output. Loss of a daemon, link or authorization must never leave a transmitter keyed. Restart/reconnect must reconcile real hardware state. Temporary slices must attach to their requested rooms without generating ghost `-rigN` rooms.

**Not part of the initial implementation:** transparent wideband IQ forwarding to every client, automatic TX authority, or general inter-server mesh resource routing.
