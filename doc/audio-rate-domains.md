# Client audio sample-rate domains

## Current implementation: two client playback buses (November 2026)

The sound-card-facing **HQ output mix** runs at either 44.1 or 48 kHz,
controlled by `audio.playback-rate` (`auto` prefers 48 kHz).
A second, separate **legacy 16 kHz stereo bus** preserves the existing
radio decoder, modem, recording and 16 kHz AF-analysis interfaces.
Its *mixed stereo output* passes through a single producer-side
band-limited 16-to-HQ sample-rate converter before joining the HQ bus.
High-rate local SDR/broadcast sources enter the HQ mixer directly rather
than being reduced to 16 kHz first. This preserves their playback
bandwidth; it does **not** change the network codec list.

```text
16 kHz radio / Kiwi / modem / legacy recording playback
                  |                                 local 44.1/48k sources
                  v                                         |
         [rx.mix — 16 kHz stereo]                           |
                  |                                         |
        [rx16.resample — 16 to HQ]                          |
                  |                                         |
                  +--------------------+--------------------+
                                       |
                          [hq.mix — 44.1/48 kHz stereo]
                                       |
                          [sink.client.dsp0 fwdsp]
                                       |
                         [sound card — 44.1/48 kHz]

[sound card microphone — 44.1/48 kHz]
                 |
      [src.client.dsp0 fwdsp]
       audioresample + downmix
                 |
      [16 kHz mono — TX/DCC/codec]
```

The client uses two independent `rr_audio_engine` mixer clocks, with
20 ms processing blocks and bounded queues. Neither clock runs in the GTK
thread. Both use S16LE for their current PCM processing contracts; a
native floating-point internal mix and clock-drift compensation remain
future work. Source resampling is band-limited and performed on its
producer side, not inside the output mixing deadline.

### Device endpoint sample-rate API

```ini
audio.capture-rate=auto
audio.playback-rate=auto
```

The client passes explicit 44100/48000 rates through
`fwdsp_audio_capture_start_rate()` and
`fwdsp_audio_playback_start_rate()`. The manager forwards `-R RATE`
to the isolated fwdsp worker. Within fwdsp, `-R` sets the named
`processor-device-caps` GStreamer capsfilter at the device boundary;
for playback it also sets `processor-src` appsrc PCM caps to the *same
rate*. Capture's codec-facing `processor-sink` continues returning
16 kHz mono PCM. **No launch-string manipulation is required by rrclient.**

Custom pipelines used with `-R` need an actual named device capsfilter.
Missing filters are treated as configuration errors rather than silently
accepting a potentially misclocked stream. The old `fwdsp_audio_*_start()`
APIs without explicit rates remain available to older callers. The
`-R` flag is accepted only in capture/playback processor mode, never for
codec, resampler or general process workers.

### Audio Routing and diagnostics

The graph shows legacy decoded `legacy.speaker` PCM routed through
`rx.mix`, `rx16.resample`, `hq.mix`, the final
`sink.client.dsp0`, independent `src.client.dsp0` microphone capture,
and the actual active playback sources. HQ sources link directly into
`hq.mix`. The rate-converter and HQ mixer are read-only topology
nodes for now; existing source gains, L/R routes and stereo speaker
trims remain editable on their owning mixer.

The graph's node details and hover properties show source PCM rate,
channels, S16LE/F32LE input representation, destination rate, queued
milliseconds, dropped frames and starvation/rebuffer counts. The HQ
worker logs its own 10-second health counters independently from the
16 kHz worker. Sound-card figures are still **requested**, not verified
hardware negotiation results: a later fwdsp caps snapshot will report
the actual negotiated rate and clock domain.

### Recording and AF bandwidth

The existing recorder and AF output spectrum taps continue to sample
`rx.mix` at 16 kHz, preserving the legacy recording format and modem
behavior. **Direct-to-HQ sources are not yet included in that legacy
pre-gain tap.** A separate rate-aware capture/recording tap is needed
before those sources can be recorded along with the entire audible HQ
mix. Do not relabel current recording as full-HQ stereo capture.

See [audio rate domain diagram](audio-rate-domains.svg) for a reference
graphic. The diagram illustrates the implemented buses and labels the
future pipeline generator separately.

## Planned architecture: dynamic per-rate AF buses

The desired long-term mixer uses native-rate buses, created only as required
by active sources. Multiple compatible sources can share a bus before
resampling its output once into a destination domain. Example buses:
8, 12 and 16 kHz for narrowband ham/SDR sources, 44.1 kHz for consumer
audio, and 48 kHz for high-quality receivers and microphone audio.

Each bus needs explicit sample format, channel layout, sample clock domain,
timestamps, latency budget, and overflow/underflow telemetry. Equal sample
rates do not imply a shared physical clock: unsynchronized sources still
require drift correction and bounded buffering. Avoid resampling when source
and destination rate and clocks are compatible. Keep complex I/Q out of
ordinary AF mixing; modem plugins operate across explicit AF/IQ boundaries.

Ultimately the ordinary AF mixing domain should favor 48 kHz float32, with
a configurable 44.1 or 48 kHz playback destination and independent mic
capture rate. Resample only at boundaries, use native-rate processing when
advantageous and preserve high-quality source bandwidth up to the selected
radio mode's limits. High-quality local broadcast listening should not
require modifying ham-radio network codec negotiation.

The Audio Routing interface is the control-plane inventory of all active
buses, rate adapters and endpoints. Each should appear as a node with
sample rate, channels, format, clock domain, buffering and health properties,
even if topology modifications are not yet supported. Never invent an
active bus or processing node merely to illustrate an architectural plan.

### Planned automatic GStreamer graph generation

A later pipeline builder can derive and validate GStreamer graphs from
typed source/sink descriptors and AF bus connections instead of hand
written static launch strings. It must support fwdsp subprocess isolation,
per-endpoint device caps, GStreamer negotiation failure diagnostics, and
runtime graph inspection. Preserve existing manual override pipelines.

For now **do not** introduce this generator and **do not** change the
16 kHz media protocol, codec negotiation or fwdsp transport.
