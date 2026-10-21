# RustyRig external DSP workers: modem and voice-codec contract

This is the architectural contract for developing new RF modulations without
requiring changes to rrclient or linking third-party DSP into rrserver.

## Ownership and fault containment

- A worker is a **local subprocess of the rrserver that owns the rig**.
  No modem audio or PTT command is forwarded to a different radio server.
- The **rig owns the worker lifetime**, independently of terminal and PTY
  clients. A serial session is merely one attachment to a running worker.
- rrserver retains authority over hardware, VFO state, PTT interlocks,
  permissions, and the rig PCM hub. Worker code never calls a backend directly.
- Worker failures must not terminate rrserver, the radio backend, the PCM hub,
  or any other worker. Monitor exit and control-FD EOF; release PTT on either.
- Restart with bounded exponential backoff. Preserve resource identity and
  client attachments, but do not replay uncertain TX commands or grant an
  inherited PTT lease to the replacement.
- Use bounded, nonblocking queues. A stuck worker must not block audio, rig
  control, or the server's network event loop.

## Types and streams

The shared `libfwdspmgr/dsp-worker.h` descriptor represents **two sides**:

| Worker family | Application side | Radio side |
| --- | --- | --- |
| PSK31 / RTTY keyboard modem | BYTES | PCM |
| Packet / ARDOP-like modem | PACKETS or BYTES | PCM |
| FreeDV / digital voice | PCM speech | PCM RF waveform |
| Experimental audio processor | PCM | PCM |

PCM formats are described explicitly: sample rate, channels, S16LE/F32LE
sample format. The audio hub should negotiate/conform formats and resample
when required. Byte streams preserve arbitrary binary octets. Packet streams
must preserve boundaries rather than silently coalescing records.

## Worker descriptor conventions (current launcher)

- fd 0: application input (stdin)
- fd 1: application output (stdout)
- fd 2: diagnostics (stderr)
- fd 3: bidirectional SOCK_SEQPACKET control
- fd 4: rig receive PCM input (rrserver -> worker)
- fd 5: rig transmit PCM output (worker -> rrserver)

The generic launch API is `rr_dsp_worker_spawn_ports()`; the legacy
`rr_dsp_worker_spawn()` uses a BYTES/PCM modem descriptor. Existing fwdsp
pipelines are *not* migrated in this first increment.

**Important:** The launcher establishes descriptors and metadata only.
It does not currently attach fd 4/5 to the rig's live PCM hub, implement
a packet frame protocol, perform PCM negotiation, or authorize fd 3
commands. Those must be implemented before claiming operational RF modes.
Configured modem subprocesses now start during rrserver serial initialization and
remain alive across WebSocket serial attach/detach. On worker failure they
restart with bounded (1–32 second) backoff. Idle output is drained so
unattached terminals do not stall the modem. Serial streams retain exclusive
ownership when attached; each device is still bound by its configured
ttyMODEM<N> export name. The complete generic rig-owned DSP manager and
PCM/PTT binding remain outstanding.

## Rig authority and control channel (next stage)

The parent process binds a worker to one rig; modem control messages may
select VFOs **inside that rig only**. Start with an operator-selected VFO,
frequency and mode, and allow explicitly granted temporary tuning control.
Keep separate permissions for RX, TX PCM, PTT, VFO selection, frequency,
mode, power, and privileged hardware commands.

The worker control protocol must include:
- hello/capability and PCM-format negotiation
- request/reply IDs and actual success/error status for VFO operations
- request/release PTT with a server-owned lease and heartbeat/watchdog
- frequency, VFO and mode get/set via the existing backend worker queue
- asynchronous rig state, signal, worker state and TX grant notifications
- explicit worker READY / RX / TX / ERROR state transitions

On process death, control EOF, timeout or shutdown: immediately enqueue the
existing prioritized PTT-off operation and block further TX PCM until a
newly authenticated/authorized worker obtains a fresh lease. The PCM hub
must not pass unauthorized audio to the transmitter.

For PSK31 AFC, distinguish changing the *audio decoder offset* from changing
the radio's RF VFO frequency. Usually only the former is needed.

## Resource and UI contract

Resource Explorer advertises each rig-owned modem/codec as a resource,
with state STOPPED, STARTING, IDLE, RX, TX, UNAVAILABLE, or FAILED and a
capability/parameter schema. A generic UI renders permitted controls from
the schema instead of hardcoding each modulation plugin.

Byte/packet modems can be attached to client PTYs and terminal tabs;
remote attachments and worker lifetime remain independent. Decoded voice
PCM goes to the existing audio path, not the serial console. The server
serial transport has acknowledged binary framing and backpressure; avoid
duplicating it. A client may attach both a PTY and VTE to the same modem
stream without opening a second modem worker.

Modems and voice codecs can later ship as **external executables** with
separately packaged descriptors and dependencies. They are not loadable
in-process server DSP plugins. A developer only needs to implement the
declared IPC contract and waveform/protocol DSP.

## Compatibility and rollout

1. Define typed ports and keep existing fwdsp interfaces unchanged.
2. Keep `modem:` workers persistent across client serial sessions (initial implementation complete); migrate to explicit rig ownership and lifecycle supervision.
3. Bind PCM RX/TX to the selected rig's hub with nonblocking bounded queues.
4. Implement the control protocol, PTT lease and rig/VFO authorization.
5. Register worker resources and typed parameters for client discovery.
6. Exercise crash, restart, stuck PCM, lost network, PTT-off and hotplug
   recovery before enabling RF TX from external workers.

The central rule is **hardware people can bring up radios without rewriting
the modem, audio, or UI subsystems, and DSP experiments cannot crash rrserver**.

## RX PCM and optional traffic logs (incremental implementation)

`rrserver_media_rx_pcm()` now forwards the existing rig receive PCM to
configured modem workers bound to that rig, with a nonblocking write to the
worker's fd 4. Configure an explicit binding with
`serial:ttyMODEM0.rig=rig0` (or use the `ttyMODEM<N>` naming fallback).
The PCM feed is currently raw S16LE mono at the server hub's expected 16 kHz;
this is **not** a negotiated media stream yet. Queue-full frames are dropped
rather than blocking radio capture. The counter is internal for now.

For replay-friendly serial capture, set
`serial:ttyMODEM0.log=/var/log/rustyrig/ttyMODEM0.log`.
The directory must already exist and be writable by rrserver. Each line is
`unix_seconds<TAB>username<TAB>session_fingerprint<TAB>TX|RX<TAB>hex`.
The session ID is a truncated SHA-256 fingerprint, **not** the authentication
token. TX means data entering the worker; RX means bytes emitted by it.
Logging is optional and is disabled when no path is configured. The format is
binary-safe but logs currently contain chunks, not parsed modem messages.
Detached worker output is discarded and is not logged in this increment.
Treat these files as sensitive: decoded chat content and operator identities
can appear in them. Ensure rotation and retention policy are configured.

Future work: explicit PCM format negotiation, bounded frame-aligned audio
queues (instead of best-effort nonblocking byte writes), async worker health,
frequency-shifted SDR slice sources, packet-granular logs, and PTT-controlled TX
audio and control socket actions. Modems are RX-only at the audio hub until
these controls are implemented.


## Canonical receive-source names (planned, not yet implemented)

A slice is a **shared immutable stream resource**, not a decoder-owned
configuration. Proposed URI-like name:

```text
rx://rig0/slice/7074000Hz/bw3000Hz/rate12000/s16le/mono
rx://rig0/slice/14074000Hz/bw3000Hz/rate12000/s16le/mono
rx://rig0/vfoA
```

Fields in the canonical slice key: **source rig / SDR front end, absolute RF
center frequency in Hz, occupied/passband bandwidth in Hz, output PCM sample
rate in samples/s, sample encoding and channel layout**. Other processing
parameters that affect samples (sideband, filter/window/resampler choices,
frequency offset, etc.) must also be part of the canonical key or separately
versioned. Avoid silently changing the content of an existing key. A VFO alias
such as `rx://rig0/vfoA` is deliberately **mutable** and resolves to a
versioned/canonical source when the operator retunes.

Multiple consumers requesting an *identical normalized slice specification*
receive the same produced PCM stream; the audio hub keeps one channelizer and
fans out to any number of independently supervised workers. If a consumer
needs a different format, sample rate or passband, either expose a separately
identified conversion node or produce a separate slice; never silently
reinterpret the original stream. Reference-count idle slices and share
common upstream filtering where possible. One slow decoder cannot block or
force audio loss for others.

Each stream also reports runtime metadata: monotonic/UTC timestamps,
sample count, true RF tuning and calibration, discontinuities, quality, and
parent RF/IQ source identity. Frequency in the name is the **RF center**,
not a free-running AF decoder offset. A decoder's AFC/AF offset is a
*consumer setting* and does not change its shared source identity. Future
FT8 scans across bands require receivers or tuning schedules capable of
covering those frequencies; a single limited-bandwidth IQ capture cannot
produce simultaneous slices on unrelated bands.

The name format is a **proposal**, not a shipped wire protocol. Normalize
and validate names before making them public; document a stable versioned
syntax and an escaping strategy for future device IDs.

## Implemented slice-identity foundation (October 2026)

`rrserver/rx.slice.{h,c}` now provides `rr_rx_slice_name()`,
`rr_rx_slice_parse()` and a reference-counted registry with
`rr_rx_slice_acquire()` / `rr_rx_slice_release()`. Identical requested
source, absolute center Hz, bandwidth Hz, rate, sample encoding and channel
layout return the **same slice object**. Changing a field creates a different
identity, not an implicit reconfiguration. The standalone
`rrserver/tests/test_rx_slice.sh` exercises name roundtrip, strict spelling,
resource sharing, and release. The stable external URI syntax should remain
versioned/documented as new IQ processing/filter parameters arrive.

**Not implemented by this registry:** DSP channelizer creation, VFO alias
resolution, PCM producer/consumer registration, bounded multi-consumer fanout,
GPU/CPU budgeting, cross-band SDR reception, timestamps or discovery APIs.
The registry currently provides identity and subscriber counts only. Those
features must be explicitly integrated with the rig PCM hub and future SDR
backend; merely acquiring a slice does not start decoding.

## Client-digital versus server-modem operating modes (October 2026)

The operator-facing mode and the actual RF modulation are separate concepts.

| Logical mode | Hardware RF mode | Digital audio owner |
| --- | --- | --- |
| D-U | PKTUSB / digital USB when directly supported | Client |
| D-L | PKTLSB / digital LSB when directly supported | Client |
| DFM | FM | Client (APRS or other FM digital waveform) |
| M-U | USB | rrserver modem subprocess |
| M-L | LSB | rrserver modem subprocess |
| MFM | FM | rrserver modem subprocess |

A rig capable of FM can accept DFM, since the radio is asked for FM; DFM
is a software processing selection, not a distinct radio CAT FM command.
Likewise, M-U/M-L/MFM should preserve their *logical* value across CAT polls
while the physical rig reports USB/LSB/FM. External mode changes on the rig
must clear an incompatible software-mode selection.

The unnumbered client-side ttyMODEM auto-connection is only appropriate for
D-U/D-L client digital operation. It must not take over the server-side M*
modem subprocess; numbered PTYs remain explicit/manual.

**Mode mapping is not yet audio integration.** M* requires selecting a
rig-bound server worker, tapping receive PCM to fd 4 *without interrupting
ordinary listener RX audio*, and admitting only that worker's fd 5 TX PCM
to the rig transmitter. Client speech/microphone TX must be isolated in
M*; modem TX must require a server-owned PTT lease and be cut on crash,
EOF, mode switch and disconnect. The existing fd 4 receive feed is
best-effort RX-only; fd 5 to RF audio and its safety controls are still
unimplemented. Do not key the transmitter or claim on-air modem TX based
only on the M* mode mapping.
