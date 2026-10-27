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
