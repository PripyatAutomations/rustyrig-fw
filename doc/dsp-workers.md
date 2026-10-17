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
