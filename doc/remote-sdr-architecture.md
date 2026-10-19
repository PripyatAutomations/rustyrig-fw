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
