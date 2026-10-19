# Remote SDR nodes, resource arbitration, and distributed VFOs

Status: architecture proposal; not an implemented protocol contract.

## Goals

An SDR node daemon (working name `rrsdrd`) runs near one or more RF devices and initiates an authenticated outbound connection to an `rrserver` link endpoint. The node offers receive and, where supported, transmit resources. The server coordinates users, VFOs, slices, room attachment, permissions, subscriptions and contention among multiple devices/nodes. This design must support receivers, transceivers and independent transmitters without assuming all hardware or slices are RX-only.

Wideband IQ normally stays on the SDR node. The node performs RF tuning, channelization/DDC, filtering and modulation/demodulation as appropriate. rrserver distributes subscribed narrowband audio or other negotiated payloads to clients without duplicating DSP or encoding for listeners of the same slice. Spectrum subscriptions use separately rate-limited/decimated data. Future server-to-server mesh routing should build on the same resource identities without requiring clients to understand physical topology.

## Identities and relationships

- **Node:** authenticated daemon instance, with stable ID, software version and heartbeat.
- **Device:** physical SDR with stable ID, driver, clocks and capabilities. A node can expose several devices.
- **RF frontend:** independently tuneable hardware channel/LO, antenna path and sample stream; some devices share clocks, tuning, power budgets or RF paths.
- **RF capture/transport:** center frequency, sample rate, actual usable passband and direction; potentially shared among slices.
- **Slice:** server-assigned UUID referring to a virtual narrowband stream within the usable RF bandwidth. Each slice declares RX and/or TX capabilities, requested frequency, occupied bandwidth, mode, DSP settings and hardware dependencies.
- **VFO:** logical user-facing tuning/control identity associated with a slice or rig, with a stable UUID and room bindings. Do not generate a fake `-rigN` room for every slice.
- **Subscription:** client or downstream server consuming audio, spectrum, telemetry or other permitted flows. Fan-out does not imply control rights.
- **Lease:** revocable authorization for changes to center frequency, slice tuning or transmission, with owner, expiry, priority, and fencing generation.

Separating these objects is essential: several VFOs can share one RF capture; one user can listen to multiple VFOs; multiple listeners can subscribe to one slice; transmitting requires its own exclusive or coordinated grants.

## Receiver placement and cooperative retuning

The coordinator maintains active slice requirements and device constraints. A proposed center-frequency change is permitted only if every protected active slice still fits within the actual usable passband (including filter roll-off/guard bands), the frontend's tuning and sample-rate limits, and any concurrent TX constraints. For example, compatible requests near 5.1 and 5.3 MHz might be covered by a 5.2 MHz capture if the device offers enough *usable* instantaneous bandwidth. This is conditional on measured device capabilities; do not assume 1 MHz nominal sampling offers 1 MHz interference-free coverage.

Plan a change first (`evaluate`): node returns feasible center/sample-rate candidates, affected slices, discontinuity and resource cost. Server arbitrates priorities and obtains/renews leases; then issues `prepare`, `commit` and acknowledges new resource generations. On failure roll back or explicitly notify affected subscribers. A new request must not silently retune existing users out of coverage. If no feasible placement exists, offer another frontend/node, a preemption request under policy, or an actionable denial.

The node is authoritative about *physical feasibility* and actual current hardware state. rrserver is authoritative about *policy, ownership and allocation*. Both sides must reconcile state after reconnect and reject stale generations.

## Transmit support and safety

Direction is a property of each device/frontend/slice/capability, not a global "SDR means RX" assumption. A node may report RX-only, TX-only or full-duplex/half-duplex operation; hardware-specific constraints include LO coupling, duplex isolation, antenna routing, power limits, frequency limits, PA readiness, external interlocks and shared frontend occupancy.

Transmit commands require distinct explicit permission and a short-lived exclusive TX lease, bound to an authenticated user/server, device, VFO, center/slice placement and fencing generation. The node **must** independently enforce frequency/range limits, regulatory/operator configuration, maximum power, duty-cycle/time-out limits, device interlocks and an immediate unkey on lease expiry, disconnect, watchdog failure or local fault. Do not trust the UI's hidden PTT control as an authorization check. Reject concurrent incompatible TX and RX placements and protect existing subscriptions according to policy. Support hardware PTT/abort and auditable start/end/rejection events. RF output must never continue merely because an rrserver or network link disappeared.

Retuning and TX authorization are separate operations; obtaining RX access never implies TX permission. Modulator input, TX stream QoS and underrun handling should be specified separately from receive audio. A device may offer RX and TX slices under the same logical room while each VFO advertises its actual controls and constraints.

## Protocol and transport

Use an authenticated outbound node-to-server session on a designated link endpoint (NAT-friendly). Build an extensible versioned control plane, not ad-hoc room messages:

- `node.hello` / `node.capabilities` / `device.inventory` / `device.state`
- `resource.offer` / `slice.evaluate` / `slice.allocate` / `slice.update` / `slice.release`
- `frontend.prepare` / `frontend.commit` / `frontend.abort`
- `lease.grant` / `lease.renew` / `lease.revoke`
- `tx.arm` / `tx.key` / `tx.unkey` / `tx.abort` (requiring node-side enforcement)
- `stream.subscribe` / `stream.unsubscribe` / `stream.format`
- `telemetry` / `health` / `fault` / `heartbeat`

Illustrative names only; settle message schemas, authorization, ordering, replay protections, timeouts, reconnection reconciliation, idempotency and capability negotiation before coding. Reuse librrprotocol transport abstraction. Control-plane ordering and durable identity must not depend on a chosen audio transport. Support constrained targets and fallback to WebSocket/TCP where QUIC is unavailable. Never send raw wideband IQ to each rrclient by default.

## Media distribution

Keep DDC, filtering and channelization near hardware. Prefer shared demodulation and one encoded AF stream per compatible slice/codec, replicated to subscribers via rrserver's media routing. Negotiate audio format/sample rate/codecs, distinguish receiver AF from transmit source AF, and handle per-subscriber routing/mixing/stereo independently of RF allocation. Spectrum/waterfall requires a separate scalable subscription and rate budget. Monitor CPU, bandwidth, DSP resource pools, slice count and latency at node and server.

## Client and room model

- Room is a collaboration/control and media attachment context, not a physical rig address.
- VFO widgets bind to UUIDs; can follow room selection or pin independently.
- RX-only VFOs omit PTT/TX-only controls; TX-capable VFOs show them only when supported *and* authorized.
- Server checks the controlling room/lease for each command. Room membership alone grants neither retune nor transmit rights.
- Support multiple RX slices per room, multiple simultaneous room listeners, and independent audio routes.
- For ordinary configured rigs with no explicit room, preserve legacy generated `-rigN` naming. Named rooms are explicit, not a temporary alias to `-rigN`.

## Incremental implementation

1. Define node/device/frontend/slice/lease models and capability records in librrprotocol; add diagnostics and strict version negotiation.
2. Implement outbound node registration, authentication, inventory, heartbeat, disconnect recovery, and server resource tables without controlling RF yet.
3. Implement RX-only allocation, shared IQ capture, center-frequency feasibility evaluation and non-disruptive recenter transactions. Attach slice UUIDs directly to selected rooms and enable AF fan-out.
4. Implement multiple frontends/devices per node, quotas, scheduling, contention feedback, shared audio encoding, and spectrum subscriptions.
5. Add TX-capable resources with node-enforced interlocks, explicit transmit leases, failsafe unkey, authorization, telemetry and audit tests. The object model and protocol must allow TX from the outset even if RX functionality ships first.
6. Extend discovery, leases and media routing across rrserver links/mesh without changing VFO UUIDs or violating single-owner hardware arbitration.

## Test cases

- Two compatible RX requests share one centered capture without interrupting existing audio.
- An incompatible third request is denied or placed on another device without silent retuning.
- A second subscriber reuses a slice and its encoder rather than duplicating hardware work.
- Stale lease/request generations, dropped links and daemon crashes cannot retune or sustain TX.
- One node advertises multiple SDR devices with distinct constraints and permissions.
- Mixed RX/TX operation obeys duplex, LO, antenna, power and hardware interlock constraints.
- A user-selected SDR room never causes a duplicate `-rigN` room.
- Capability downgrade, reconnect and server takeover reconcile or revoke resources safely.

## Non-goals initially

Full inter-server mesh routing, transparent wideband IQ forwarding, automatic permission to transmit, and unrestricted preemption of existing users.
