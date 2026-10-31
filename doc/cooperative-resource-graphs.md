# Cooperative Resource Graphs (CRG) — architectural draft 0

Status: exploratory design, **not** an implemented feature or frozen wire ABI.

## Mission: cooperative clusters

RustyRig should let a ham or small group assemble independently owned stations into a cooperative cluster, grant other people access to selected resources, and build experimental signal paths without writing an entire radio application. Each station retains final authority over its equipment. A relay is not an owner merely because it forwards a resource.

Simple operations (listen, tune, change mode, select speakers) must remain simple. Advanced users may create arbitrary authorized graphs, presets and custom radio definitions.

## Ownership and component boundaries

- **librrprotocol**: future versioned, portable resource, stream, graph, and grant descriptors. Native C structures are *not* network serialization.
- **librrsdrutils**: standalone DSP primitives, typed I/Q formats, NCO, filters, decimators, resamplers, FFT and channelizers. No GUI, networking, users, GStreamer, or hardware SDK dependencies.
- **rrsdr** (proposed executable): owns IQ acquisition, executes shared receive/slice graphs near SDRs, and exposes slices through local control/stream interfaces. Supports USB, PCIe and remote network-attached hardware through adapters.
- **rrbridge**: isolated adapters for externally managed receivers (e.g., KiwiSDR, WebSDR). An rrsdr service may be connected to rrserver without *requiring* rrbridge as an extra hop. Adapters share common descriptors.
- **rrserver**: signed resource discovery, authorization, owner-side control arbitration, graph planning and placement, worker supervision, mesh/media-session orchestration.
- **rrclient**: ordinary rig/VFO UI and advanced graph/patchbay editor; local audio routing is a personal graph, not station-wide hardware reconfiguration.
- **fwdsp / libfwdspmgr**: existing audio/modem process integration should be reused, not duplicated. **GStreamer remains optional.** It is a useful pipeline runtime, but graph execution and SDR slicing must not depend on GStreamer. Run potentially unstable pipelines in separately supervised processes; use bounded local IPC and appropriate scheduling so they cannot stall hardware capture or the server's main event loop. Process isolation improves fault containment, not automatically real-time performance.

## Uniform resources

Device -> hardware RF channel -> slice -> output stream -> subscription. Devices, channels, slices and graph instances have distinct owner-scoped identities. Running graphs also have a generation identifier to reject delayed commands and media from replaced configurations.

Resource descriptions distinguish capabilities, current state, configuration, shared dependencies (LO, clock, antenna, ADC, TX chain), and authority. Hardware DDCs/channelizers are capabilities the graph planner may use; it should not assume software processing is always required.

## Graph contract

Each node has typed input/output ports. A request describes an outcome (frequency, bandwidth, mode, media type, format, target device) and optional constraints, not a hard-coded processing chain. The planner may insert mixers, converters, resamplers, decimators, demodulators, codecs or transport edges.

Plan -> authorize -> reserve -> instantiate -> verify -> activate -> publish. Changes should not disrupt unrelated graphs; on failure preserve the previous viable version. Shared capture or FFT stages may feed many subscribers without redundant work. All queues are bounded; slow network clients never block acquisition.

Execute DSP nearest the IQ source when practical. Placement is subject to owner grants, trust boundaries, performance budget and latency. Explicit expert graphs remain possible but pass the same authorization and resource validation.

## I/Q over RustyRig media transport

I/Q is a first-class media session profile alongside audio, spectrum, video and decoded-data streams. Negotiate CU8/CS16/CF32 initially, sample rate, RF reference center, bandwidth, sample-clock/time domain, channel count, sequence, continuity/discontinuity and graph generation. Sliced I/Q may be much narrower than hardware acquisition. Reuse existing direct/proxy transport decisions and avoid sending raw wideband I/Q across a mesh by default.

The existing rrbridge/spectrum.row.h is native, same-build local IPC only and must not become a network wire contract. Separate source-side stream descriptors from packet serialization.

## Independent control and permission domains

- Slice tuning within a captured band should not retune shared RF hardware.
- Hardware center, ADC rate, gain, clock, antenna, and TX controls are separate resources with independently granted authority.
- Conceptual permissions (names provisional): sdr.listen, sdr.slice.create, sdr.slice.control, sdr.capture.center, sdr.capture.sample-rate, sdr.capture.gain, sdr.device.admin, sdr.tx.authorize, sdr.tx.hardware.
- Resource owner validates every privileged operation, including relayed commands. Delegated leases are scoped, time-limited and revocable. A compromised relay may drop/delay but must not forge an authorized action.
- Reconfiguring hardware that disrupts active slices must require policy/coordination. TX scheduling, PTT interlocks, transmitter exclusivity and fail-safe release live outside DSP algorithms; arbitrary graph nodes cannot directly key transmitters.

## Execution interfaces

A standalone rrsdr should support Unix SOCK_SEQPACKET for control/events, shared-memory bounded rings with eventfd for high-throughput local I/Q, and simple pipe/stream adapters for tools. Remote IQ is carried via RustyRig media sessions. Driver implementation has two entry levels: simple acquisition/control callbacks and advanced hardware-accelerated processing-node advertisements. Local device plugins and experimental GStreamer pipelines can run out of process under supervision.

## Personal and shared graph editors

Users may route multiple receivers to distinct speakers, mixers, audio devices and local processing pipelines, with one-click restore-to-default. Save graph presets referencing stable resource IDs with offline fallbacks. Future rig-definition and GStreamer editors can emit validated declarative definitions, but untrusted user pipelines must not inherit hardware-control or TX privileges.

## Initial milestones

1. Agree on resource/node/stream terminology and separation boundaries.
2. Add experimental independent librrsdrutils descriptors and pure validation tests without changing existing behavior.
3. Synthetic wideband IQ source with multiple independent slices; verify rates, isolation, discontinuities, and sharing.
4. Standalone rrsdr control/IPC and server adapter, then IQ media-profile negotiation.
5. RTL-SDR hardware adapter and two simultaneous independent users; prove one user cannot retune hardware without a grant.
6. Later hardware-assisted channelizers, UHD, SoapySDR and more complex TX/RX resources.

## Open decisions

- Exact portable serialization and schema versioning in librrprotocol.
- Planner/executor separation and integration with existing libfwdspmgr supervisors.
- Clock domains, rational rates, sample-accurate timestamps and frequency precision.
- Channelizer cost model (per-slice DDC vs polyphase banks), compute placement, HW acceleration.
- Shared memory ring ownership, lifetime and crashing-worker recovery.
- Wideband IQ transport encoding, network loss and resynchronization.
- Lease and multi-user hardware-retune arbitration; TX safety/regulatory station-control policy.
- How existing VFO/catalog identities map onto graph resources without duplicate objects.

**No existing media or bridge protocol change should be inferred from this draft.**
