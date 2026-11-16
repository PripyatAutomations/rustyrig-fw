# Routing pin and wire colors

The **Pin colors** button at the bottom-right of the Routing tab (beside the
touch zoom buttons) opens this legend without leaving the graph.

| Color / style | Type | Meaning |
| --- | --- | --- |
| **Cyan / blue** | PCM mono or left | Audio samples; the left channel of stereo streams |
| **Purple** | PCM right | The right channel of stereo streams |
| **Red** | PTT | Logical push-to-talk control **messages**; not an audio stream, and not a report that a radio is transmitting |
| **Yellow** | COS | *Carrier-operated squelch* or authoritative synchronization lock; logical **receive detection** output |
| **Green (dashed)** | Parameter control | Local slider/knob to gain or LV2 parameter; not audio |
| **Orange (dashed)** | Bypass | Standby/fallback audio connection, used if the normal source fails |
| **Black (dashed)** | Serial CAT / GPS | Local PTY bindings between virtual rigs and selected remote resources; not raw serial tunneled across the mesh |

A **filled pin** is an output and a **hollow ring** is an input. Dashed line
styles help differentiate the link families without relying on color alone.
The graph should validate compatible *types*, never infer compatibility from
visual proximity alone.

## COS design contract

A COS output belongs on a receive/demodulator block **only when the backend
has a definite, authoritative detection state**. That includes:

- **FM:** carrier-operated squelch (or a configured reliable FM squelch-open
  detector), with appropriate threshold and hysteresis.
- **AM:** a carrier-detection or squelch-open detector, *not merely selected AM
  mode* or the presence of audio noise.
- **Synchronized digital modes:** the mode's validated frame synchronization
  or signal-lock condition, with defined acquire/release behavior.

The yellow output is **asserted while that specific receiver detects a
qualifying signal**, and deasserted on loss of carrier/sync, source removal,
disconnect, or detector reset. It is not driven by VFO TX/PTT, microphone
activity, generic audio energy, or a different receiver's detection state.
Every COS control message must identify its originating resource/pin, so
consumers observe only the wire they are actually connected to.

**Current implementation:** the shared GTK graph primitives reserve and
render yellow COS pins and COS wire types for providers that explicitly
advertise a detector. At present the RX audio graph does **not** export a
verified AM/FM/sync-detection signal; don't manufacture a live COS output
until the receiver's actual detector and source-qualified event path have
been connected. No permanent COS state should be inferred just from a
selected radio mode.

The ON AIR indicator is for **PTT messages from its specific wired red pin**;
it deliberately does not follow COS or the radio's reported TX state.
