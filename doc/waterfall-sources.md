# Spectrum and waterfall sources

A waterfall is a **view of a spectrum source**, not a KiwiSDR feature. Multiple
sources may be attached to the same room/VFO and rendered by the same dockable
or floating GTK panel (and ultimately the browser WebUI).

## Source model

Each source advertises `source.id` (stable per producing endpoint),
`owner.node`, `room`, `rig.uuid`, `vfo.uuid`, `kind`, `bins`, `units`,
`sample-rate` (where applicable), `frequency.low`, `frequency.high`,
`epoch`, and an optional `tuneable` flag.

- **AF-local:** client computes an FFT/STFT of *decoded RX PCM before the
  room's routing and speaker gain*, independently for each subscribed RX stream.
  No upstream waterfall subscription or server CPU is required. The axis is
  audio frequency (0..sample_rate/2 for mono PCM); *never* label it as an RF
  sweep. Use an absolute offset from the tuned carrier only when demodulation
  metadata explicitly defines the mapping. Default 16 kHz mono PCM spans 0–8 kHz.
- **AF-server:** server (or an external DSP source) publishes a demodulated
  audio spectrum for a receiver. Axis is audio frequency. Useful for thin
  clients and WebUI receivers that do not perform local PCM FFT.
- **RF-server:** an SDR driver, an external SDR node, or a physical transceiver
  publishes spectral rows from its RF receiver. Axis is absolute RF frequency;
  the data provider controls usable span, resolution, and zoom limits.
- **IQ-derived:** an SDR backend may eventually compute its own wideband
  spectrum from IQ data. Identical downstream shape to RF-server.

Do not invent separate Kiwi, RTL-SDR, SoapySDR, etc. rendering protocols.
Providers adapt their native FFT/waterfall formats to the generic spectrum row.

## Spectrum row contract

Use the existing media subsystem `RR_BINFRAME_SUBSYS_WATERFALL` (0x03) with
an explicit versioned format negotiated in the media catalog, for example
`sp08` (8-bit log-power levels) initially and `sp16` for higher dynamic
range. Do **not** treat waterfall rows as an ordinary video codec.

A row has a timestamp, source/stream identity, frequency minimum/maximum,
bin count, bin-center conventions, logarithmic power reference and quantization
scale, sequence number and data bytes. The owner/room/VFO relationships are
published once in the channel catalog. Metadata updates (retune, sample-rate,
FFT length/zoom, frontend selection) advance an epoch; clients discard stale
rows for previous epochs.

Producers must bounds-check bin counts, bandwidth, frame sizes and rate.
Consumers reject malformed/mismatched epochs. When the panel is hidden or
has no subscribers, producers should avoid FFT computation or upstream WF
connections. If several viewers share a stream, rrserver fans it out once.

## AF client pipeline

Decoded mono PCM -> bounded single-producer queue -> local spectrum worker ->
Hann-window STFT -> dBFS magnitude -> generic spectrum-row ring -> GTK or WebUI
renderer. Tap **before** mixing/volume adjustments so an AF panel targets one
room/stream even if the user has muted it or panned it to one ear.

Default FFT 1024 or 2048 at 16 kHz, 50–75% overlap and ~10–20 visual rows/s.
Provide configurable FFT size, averaging, floor/ceiling, color gradient and
history duration. Use a bounded queue and drop old spectrum input under load:
the RX audio worker must not block on drawing or FFT. A waterfall panel must
not affect the independently timed 20 ms PCM playback engine.

For mixed audio, optionally add an explicit `mixed audio` source; this is not
interchangeable with an individual receiver AF waterfall.

## UI lifecycle

The floating/dockable panel is keyed to a room/VFO plus *selected spectrum
source*. Offer an explicit selector, for example `Audio (local)` and
`Receiver RF (server)`, with both open simultaneously if desired. Preserve
source selection, dock state, zoom, gain and color settings across tab changes.
Closing the panel un-subscribes only its waterfall source; it must not stop
RX audio or destroy the receiver.

RF clicks may request tuning only if the selected source advertises tuning
capability and the operator has a resource grant; AF clicks display audio
frequency or configure a separately authorized audio filter. Never interpret
an AF bin as an absolute RF tune request.

## Backend responsibilities

- `rrbridge-kiwi` may open a separate `W/F` connection when there is demand,
  normalize its spectral rows and publish through a generic host callback.
- Future WebSDR bridges do the same with their own native protocols.
- Hardware SDR/rig adapters may produce power bins from native waterfall APIs
  or local IQ. The renderer does not know or care which driver produced them.

## Implementation order

1. Add common spectrum source descriptor, row validation and test vectors to
   librrprotocol; preserve existing audio/video routing.
2. Add per-RX-stream local AF FFT tap on decoded PCM and bounded worker queue.
3. Implement reusable spectrum-row viewer with a GTK dock/float lifecycle.
4. Add server-side spectrum publication/subscriptions (demand-driven) and a
   first RF provider, Kiwi `W/F`.
5. Expose the identical catalog and row decoding in browser WebUI.

Acceptance tests: no audio underruns from FFT load, source switch while playing,
reconnect epochs, wide/narrow frequency spans, simultaneous AF/RF panes, mute
without stopping analysis, no server WF connection when no subscribers, and
safe teardown on deleted SDR rooms.

## Direct local Kiwi and per-VFO floating panels (October 2026)

The local client now has an independently floating local Kiwi tuning panel.
The legacy rig VFO widget remains a singleton driven by global room selection.
Do not clone `create_vfo_box()` as-is: its global frequency/PTT/mode
pointers and room-bound CAT senders would make windows control each other.
For arbitrary VFO windows, introduce a per-panel model:

- Identity: (provider instance ID, receiver or rig UUID, VFO ID), not a
  selected chat tab. Local instances must have independent stable IDs.
- Each panel owns its frequency/mode widgets, receive-only/TX capabilities,
  tuning completion state, subscription token, and spectrum source selection.
- Tuning dispatch goes through the bound provider adapter (local bridge IPC or
  remote server control); never through the global selected room.
- Destroying the panel removes only its UI subscriptions; it does not destroy
  a receiver shared with another panel. Closing the last local receiver
  consumer may separately trigger receiver teardown by explicit policy.

### Kiwi remote RF waterfall integration requirements

1. Open Kiwi's *separate* waterfall WebSocket (`W/F`) alongside the existing
   audio WebSocket (`SND`) in `rrbridge/module.kiwi.c`. Drive both sockets
   from the bridge's existing Mongoose manager; keep distinct connection
   pointers, handshake/configuration states, retry counters, and watchdogs.
   Audio must stay online if the waterfall connection fails.
2. Decode native waterfall binary rows into a common spectrum-row contract.
   Include receiver identity, center/span or min/max Hz, bin count, power
   quantization/reference, frame timestamp, and retune epoch. Validate row
   sizes before allocating. Native bins are RF—not audio FFT.
3. Add a bounded spectrum transport from bridge to its host, separate from the
   PCM socket and control IPC. Prefer a third `SOCK_SEQPACKET` FD with
   versioned spectrum headers; never block `SND` polling on spectrum frames.
   Extend `rrbridge` host callbacks/capabilities and increment their ABI
   versions together with the host.
4. For local clients, feed the normalized rows into the GTK spectrum ring.
   For remote clients, make the server publish the same rows through
   `RR_BINFRAME_SUBSYS_WATERFALL`, with explicit subscription interest,
   bounded rate and fanout (one upstream W/F stream per receiver, not per
   listener).
5. The floating VFO gets a compact RF spectrum strip at its top. It appears
   only after a source descriptor and a validated fresh row arrive; hide it
   when unsupported, unsubscribed, stale, or disconnected. A full waterfall
   window can share the same source/ring. The axis must show real RF Hz,
   including tuning changes and zoom/span updates.
6. Add tests for fragmented/invalid W/F frames, endianness, bin-count
   mismatch, epoch discontinuity, reconnect, retune, source switching,
   and multi-subscriber fanout. Constrain frame rates so heavy RF drawing
   cannot delay audio IPC or RTP polls.

**Important:** the new floating local Kiwi panel currently provides tuning,
not a spectrum. Its RF strip stays absent until the above data path exists.
