# RustyRig Transport and Module Architecture

## Status and purpose

This document defines the proposed architecture for removing direct Mongoose dependencies from `librrprotocol`, `rrclient`, and `rrserver`, while preserving a complete Mongoose build and permitting alternative transports to be selected at compile time. It also defines the work needed to complete the loadable-module facility in `librustyaxe` and initialize it from both applications.

This is an architecture and implementation handoff, not an implementation. The implementation baseline is the latest `rustyrig-fw` `main` branch, inspected here at commit `ae40582`, with `librrprotocol` at `66b47eb` and `librustyaxe` at `7a8c1a2`. The developer is actively working on the `new_audio` branch, inspected here at commit `3220159`. Before modifying code, fetch and re-audit both current heads. Implement against current `main`, then reconcile the affected audio, media-channel, fwdsp, connection, build, and event-bus changes from `new_audio`; do not treat the inspected commits as frozen targets.

The two RustyRig branches currently have substantially different tree layouts and do not share a merge base in the shallow inspection clone. Do not mechanically apply paths or diffs from this document. Use architectural ownership and symbol searches to locate the corresponding current files.

## Decisions

The first priority is a transport abstraction. WebTransport is a consumer of that abstraction, not the abstraction itself.

The following decisions are normative:

1. `librrprotocol` must not include Mongoose headers, use Mongoose types, or conditionally compile protocol behavior around `USE_MONGOOSE`.
2. `rrclient` and `rrserver` application and protocol code must not use Mongoose objects directly. Backend-specific translation units may do so.
3. The Mongoose backend remains complete. A Mongoose build must retain every currently supported Mongoose-based feature: HTTP serving, HTTP API, WebSocket control, WebSocket binary media, TLS configuration, connection management, and any enabled MQTT behavior.
4. Backends are selected at compile time. Runtime selection between independently compiled backend implementations is optional and must not be required for the initial migration.
5. A connection is an application/session object containing RustyRig state. A transport handle is private backend state. These must not be the same object.
6. Control messages and media messages use one common transport API but retain different delivery requirements.
7. WebTransport reliable streams and datagrams may be used simultaneously within one session. Switching a media channel between them must not require recreating the authenticated RustyRig session.
8. Application code must express required semantics, not backend mechanics. It asks for reliable ordered messages, unreliable datagrams, HTTP responses, or closure; it does not call WebSocket, QUIC, or Mongoose functions.
9. The `librustyaxe` module loader must become generic. It must not include `librrprotocol` or require `rrconn_t` merely to load a module.
10. Dynamic modules are a host extension mechanism. They are not transport backends in the first implementation. Transport backends remain compile-time components with static vtables so the core dependency graph stays predictable.

## Current coupling that must be removed

The current trees expose Mongoose through several architectural boundaries:

- `librrprotocol/rrprotocol.h` conditionally includes `mongoose.h`.
- `rrconn_t` in `librustyaxe/struct.h` contains Mongoose connection pointers.
- `librrprotocol/connman.h`, `http.h`, and `ws.h` expose `struct mg_connection`, `struct mg_mgr`, `struct mg_str`, `struct mg_ws_message`, WebSocket opcode constants, and Mongoose event values.
- Protocol serialization and dispatch files call `mg_ws_send()` directly.
- Client and server connection creation, lookup, and removal use the Mongoose pointer as connection identity.
- HTTP routes accept `struct mg_http_message` rather than a neutral request view.
- Some non-transport facilities use Mongoose utilities, including SHA-1 and JSON accessors.
- `rrclient` drives Mongoose directly from a GLib timeout.
- `rrserver` owns a global `mg_mgr` and polls it directly.
- fwdsp subprocess pipes are represented as Mongoose connections even though they are local I/O, not network protocol sessions.

The migration must remove each kind of coupling separately. Replacing only `mg_ws_send()` would leave the public types, lifecycle, HTTP routing, crypto helpers, event loop, and local subprocess I/O coupled to Mongoose.

## Proposed component boundaries

### librustyaxe

`librustyaxe` owns general-purpose facilities:

- configuration
- dictionaries and JSON conversion
- logging
- event bus
- generic connection metadata types, if they truly belong in a general library
- module discovery, loading, lifecycle, and unloading
- portable dynamic-library support

It must not own WebSocket, HTTP, WebTransport, QUIC, authentication, radio protocol, or Mongoose concepts.

### librrprotocol

`librrprotocol` owns RustyRig protocol behavior:

- authentication message generation and processing
- JSON control message dispatch
- binary frame encoding, decoding, and dispatch
- chat, CAT, status, syslog, and media-channel protocol logic
- server session policy and client protocol state
- broadcast selection and subscription selection

It may depend on the public transport abstraction, but not on a concrete backend.

### transport core

The transport core owns neutral handles, lifecycle, events, buffers, capabilities, and the compile-time backend contract. It should be a small library or a clearly isolated part of `librrprotocol`. A separate `librrtransport` is preferable if embedded builds or other projects may reuse it.

### backend implementations

Each backend translates neutral operations and events to a concrete library:

- Mongoose backend
- native WebSocket backend, likely HTTP/1.1 plus Wslay
- WebTransport backend, initially targeting Picoquic/H3zero/Picowt
- test backend using memory queues

Only backend implementation files may include the corresponding third-party headers.

### applications

`rrserver` owns server policy, listeners, configuration, radio control, audio pipelines, databases, and process management.

`rrclient` owns connection profiles, reconnection policy, UI integration, audio pipelines, and the choice of requested media delivery mode.

Both applications initialize one compiled transport backend through the same public API.

## Public transport object model

The API needs the following opaque types. None exposes backend layout.

| Type | Purpose |
| --- | --- |
| Transport runtime | Backend-wide state, sockets, timers, TLS contexts, and event-loop integration |
| Listener | One server listening endpoint, such as HTTPS and WSS over TCP or WebTransport over UDP |
| Session | One peer relationship and its lifecycle; the object attached to RustyRig session state |
| Channel | One logical delivery path within a session, such as the reliable control path or a media path |
| Request | Read-only neutral HTTP request view valid for a documented callback lifetime |
| Response | HTTP response builder or send operation |
| Buffer | Payload view with explicit ownership and lifetime |
| Address | Neutral local or peer address and port |

The RustyRig `rrconn_t` should contain a transport session pointer, not a Mongoose pointer. Backend state belongs behind the opaque session. Ideally `rrconn_t` eventually moves from `librustyaxe` into `librrprotocol`, because its authentication, chat, VFO, codec, room, and media subscription fields are RustyRig-specific.

## Backend descriptor and compile-time selection

Each backend supplies a constant descriptor containing:

- ABI version and descriptor size
- backend name and version string
- capability flags
- runtime create and destroy operations
- listener create and destroy operations
- client connect operation
- poll or event-loop integration operations
- send, close, and channel operations
- HTTP response operations when HTTP serving is supported
- optional diagnostic operations

The core build chooses exactly one default backend descriptor for each application binary. Multiple descriptors may be linked for test programs, but ordinary binaries should not require runtime plugin discovery.

Recommended build identities are `TRANSPORT_MONGOOSE`, `TRANSPORT_NATIVE_WS`, and `TRANSPORT_WEBTRANSPORT`. Do not propagate these defines into protocol files. They select object lists and one descriptor in the build system. A backend source file may use its own dependency-specific definitions internally.

## Capability model

The runtime and each session expose capabilities. Capability discovery must be queryable rather than inferred from the backend name.

Minimum capabilities are:

- HTTP server
- HTTP client, if required
- static file serving
- REST routing
- reliable message channel
- reliable byte stream
- unreliable datagrams
- peer-initiated streams
- TLS
- native ping and pong
- per-message text and binary distinction
- graceful close reason
- backpressure notification
- peer address
- HTTP origin and header inspection
- MQTT, if Mongoose MQTT remains part of a supported build

Capabilities describe what the compiled backend can do and what the negotiated session can do. For example, a WebTransport-capable backend may produce a reliable-only session if datagrams were not negotiated.

## Data delivery classes

The public API should define semantic delivery classes:

| Delivery class | Required behavior | Likely mapping |
| --- | --- | --- |
| Control message | Reliable, ordered, message boundaries retained | WebSocket message or framed WebTransport stream |
| Reliable media | Reliable and ordered, frame boundaries retained by RustyRig framing | WebSocket binary message or framed WebTransport stream |
| Realtime datagram | Unreliable, unordered, one application frame per datagram | WebTransport datagram |
| Bulk stream | Reliable byte stream with backpressure | Dedicated WebTransport stream; future use |

The protocol layer continues to own RustyRig binary headers. The transport layer does not interpret codecs, VFOs, channel UUIDs, chat commands, or CAT messages.

Datagram sends need an explicit result distinguishing accepted, temporarily blocked, too large, unsupported, closed, and fatal error. Realtime media should normally drop on temporary blockage rather than accumulate latency. Reliable sends may queue up to configured limits and must expose a writable or drained notification.

## Required public operations

The initial API should cover these operation groups. Names below are conceptual and may be adjusted to project naming conventions.

### Runtime lifecycle

- Create a runtime from a backend descriptor and neutral configuration.
- Start and stop the runtime.
- Poll until a supplied deadline, or expose waitable file descriptors and next-deadline information.
- Request wakeup when another thread queues work.
- Destroy the runtime after all listeners and sessions have closed.
- Query backend identity, version, compiled capabilities, and last diagnostic error.

The runtime API must accommodate both polling applications and GLib. A GLib adapter belongs outside the backend-neutral core and should register the backend's waitable descriptors and deadline. A simple periodic poll is acceptable during migration but should not be the permanent contract.

### Server listeners

- Listen using a neutral URL or structured endpoint configuration.
- Supply certificate, private-key, CA, client-certificate, ALPN, and verification policy without exposing a TLS-library structure.
- Register HTTP routes and WebSocket or WebTransport session paths.
- Stop accepting new sessions while allowing existing sessions to drain.
- Query bound addresses and actual ports.

TCP and UDP listeners may coexist. A deployment may use TCP 443 for HTTPS and WSS and UDP 443 for HTTP/3 and WebTransport.

### Client connection

- Connect using a URL, desired protocol set, TLS policy, headers, origin, and application user pointer.
- Cancel an in-progress connection.
- Report DNS, TCP or UDP, TLS, HTTP upgrade, WebTransport establishment, and application-ready stages through neutral events.
- Support reconnection by creating a new session rather than mutating a dead backend handle.

### Session lifecycle

- Attach and retrieve an application pointer.
- Retain and release the session if asynchronous callbacks can outlive the initiating stack frame.
- Query state, negotiated protocol, capabilities, local and peer address, close code, and close reason.
- Request graceful close or immediate abort.
- Send a reliable text or binary message.
- Send a datagram.
- Open a reliable unidirectional or bidirectional channel when supported.
- Request notification when queued reliable data falls below a threshold.

Application code must never search for a session by backend pointer. The backend attaches the `rrconn_t` pointer as session user data, and the application owns its own connection indexes.

### Channel lifecycle

- Open or accept a channel with a purpose identifier and delivery properties.
- Attach application data.
- Send framed messages or byte-stream data, depending on the channel mode.
- Half-close, reset, or close the channel.
- Query queued bytes and writable state.

The first WebTransport implementation may use one long-lived reliable control stream plus datagrams for realtime media. The API should not force every future reliable message through the WebTransport CONNECT stream.

### HTTP request and response

The neutral request view must provide:

- method
- URI path
- query
- HTTP version where meaningful
- headers by case-insensitive lookup
- body view and length
- peer address
- TLS state
- route parameters if routing is in the core
- upgrade request information

The response API must provide:

- status
- headers
- fixed body
- incremental body or file response
- explicit completion
- WebSocket upgrade when supported
- error response helper

Static serving may initially remain a backend convenience operation, but route matching and RustyRig API handlers must consume neutral requests. A Mongoose backend can still delegate efficient file delivery to Mongoose internally.

## Event contract

All backend events are translated into a compact neutral event set:

- runtime error
- listener error
- session connecting
- session established
- session ready
- session closed
- reliable message received
- datagram received
- channel opened
- channel data received
- channel writable
- channel closed or reset
- HTTP request received
- native ping or pong information, when available

Each event specifies payload ownership and callback lifetime. Payload views are borrowed during synchronous callbacks. Any event posted to another thread or main loop must carry owned data. Session and channel handles must remain valid for the callback, with explicit retain/release rules for later use.

Transport callbacks should enter `librrprotocol` through a small adapter that:

1. retrieves the associated `rrconn_t`;
2. translates reliable text into JSON protocol dispatch;
3. translates reliable binary or datagram media into binary-frame dispatch;
4. invokes application lifecycle hooks;
5. never exposes the backend event object to protocol handlers.

## Backpressure and queue policy

Backpressure is part of the public contract, not a backend accident.

Each session and channel should support configurable limits for queued bytes and queued messages. The reliable send operation must return immediately with a defined result. The backend emits a writable notification when progress is possible.

Realtime datagrams are not queued indefinitely. The recommended policy is:

- send immediately when possible;
- drop when blocked or above the current maximum datagram size;
- count drops by reason;
- never delay later audio behind an old datagram;
- allow the media layer to request reliable delivery instead.

The API should expose path maximum datagram payload when known. RustyRig can then choose Opus framing parameters that fit without fragmentation.

## Protocol mapping

Control and authentication use a reliable ordered message channel on every backend. Existing JSON dictionaries remain the canonical application representation.

For WebSocket sessions:

- JSON uses text messages.
- RustyRig binary frames use binary messages.
- WebSocket ping and close may map to native operations.

For WebTransport sessions:

- JSON uses a framed reliable bidirectional stream.
- reliable media uses framed messages on a dedicated reliable stream or channel.
- realtime media uses datagrams.
- a small channel preface identifies the RustyRig channel purpose and framing version.
- switching media delivery changes the selected channel or delivery class, not the authenticated session.

The `media.capab` and channel-selection protocol should advertise delivery capabilities independently from codecs. A useful conceptual split is:

- codec capabilities: `pc16`, `mu16`, `mu08`, `opus`, `flac`
- delivery capabilities: reliable message, reliable stream, realtime datagram
- channel selection: codec plus delivery class

Fallback order should be configured per channel. For live Opus audio, datagram then reliable message is a reasonable default. For FLAC recording transfer, reliable delivery is required.

## Mongoose backend completeness

The Mongoose backend is not a compatibility stub. Completion criteria include:

- all existing HTTP routes behave the same;
- static files, custom headers, MIME handling, and 404 behavior remain available;
- HTTP and HTTPS listeners retain current configuration behavior;
- WebSocket control and binary media retain message boundaries and close behavior;
- authentication, peer address, user-agent bans, limits, and timeouts remain available;
- existing MQTT behavior remains available when compiled;
- rrclient connection and TLS verification behavior remains available;
- embedded targets can omit all non-Mongoose dependencies;
- protocol test vectors pass identically through the Mongoose and memory backends.

No `#ifdef USE_MONGOOSE` should remain in public protocol headers or ordinary protocol handlers. Mongoose conditionals are permitted in build files and Mongoose backend implementation files.

## Native WebSocket and WebTransport backend composition

The likely non-Mongoose composition is:

- Wslay for RFC 6455 framing;
- a small HTTP/1.1 and WebSocket-upgrade layer selected separately;
- Picoquic, H3zero, and Picowt for WebTransport;
- OpenSSL or the TLS provider required by the selected QUIC stack;
- a thin common socket and timer adapter integrated with the application loop.

Wslay does not perform the HTTP opening handshake, so choosing it does not settle HTTP parsing, static serving, TLS-over-TCP, or route handling.

Picoquic/Picowt is the leading WebTransport candidate because it exposes WebTransport sessions, streams, and datagrams rather than only QUIC and HTTP/3 primitives. LSQUIC should remain an interoperability comparison, especially after its current HTTP Datagram and WebTransport updates land. Imquic is useful reference material for a GLib-facing wrapper but is too explicitly experimental and platform-limited to be the foundational dependency today.

## Non-network Mongoose removals

Removing Mongoose from protocol code also requires replacements for incidental utilities:

- SHA-1 and authentication hashing must use a dedicated crypto abstraction or a small permitted implementation. Authentication compatibility must be tested against existing stored credentials and wire challenges.
- JSON parsing must use `librustyaxe` dictionary and JSON APIs exclusively.
- string views need a project-owned byte-span type.
- URL parsing and encoding need neutral helpers.
- subprocess stdout, stderr, and control pipes must use the application's event loop or a generic I/O watcher, not fake network connections.
- time and timers must use the existing timespec/event facilities rather than Mongoose timers.

These tasks should be tracked separately so the transport migration does not accidentally retain Mongoose through a utility include.

## librustyaxe module architecture

### Problems in the current loader

The current module implementation is an unfinished proof of concept:

- public declarations for load, unload, enumerate, and lookup are absent from `module.h`;
- the loader includes `librrprotocol`, creating an inappropriate dependency direction;
- the API version constant is private and not validated against a module descriptor;
- `modinfo` is looked up using the event structure type rather than a dedicated descriptor type;
- `modexports` is found but never registered with the event bus;
- initialization and shutdown callbacks are not invoked;
- unload is unimplemented;
- allocations and `dlopen` handles leak on several failure paths;
- `dlerror()` is not used for diagnostics;
- module names, paths, duplicates, and state transitions are not validated;
- listener registrations are not tracked, making safe unload impossible;
- `RTLD_GLOBAL` exposes avoidable symbol collisions;
- there is no Windows dynamic-library adapter;
- callback prototypes use unspecified argument lists;
- there is no protection against unloading while callbacks are in flight.

### Module descriptor

Each module should export one versioned descriptor symbol. The descriptor contains:

- descriptor size
- module ABI major and minor version
- stable module identifier
- display name, description, version, copyright, and license
- flags, including unload-safe and static-module indicators
- optional load, start, stop, and unload lifecycle callbacks
- optional array of event subscriptions
- optional command or service exports defined through explicit extension interfaces

The loader validates descriptor size, ABI range, required fields, duplicate identifier, and supported features before invoking module code.

### Host API passed to modules

Modules should receive a versioned host API table instead of resolving arbitrary host globals. It should initially expose:

- logging
- configuration lookup
- event subscribe and unsubscribe
- event emit operations
- memory allocation only if cross-boundary ownership requires it
- monotonic and wall-clock time
- module-owned cleanup registration

Application-specific services should be registered as named, versioned interfaces. For example, `rrserver` could register a radio-control service and `rrclient` could register a UI-notification service. `librustyaxe` itself remains unaware of either.

### Lifecycle

Module states should be discovered, loaded, initialized, started, stopping, and unloaded, with failed as a terminal or diagnostic state.

The lifecycle is:

1. Resolve a configured module path safely.
2. Open the library with local symbol visibility by default.
3. locate and validate its descriptor.
4. allocate the loader's module record.
5. call load or initialize with the host API.
6. register declared subscriptions and record every registration.
7. call start after the application's core services are ready.
8. on shutdown, prevent new callbacks, unregister subscriptions, wait for in-flight calls, call stop and unload, close the library, and free the record.

If a module does not declare itself unload-safe, it may be stopped during application shutdown but not hot-unloaded.

### Static modules

Embedded or restricted builds should support the same descriptor compiled statically. A static registration table supplies descriptors to the same validation and lifecycle engine without `dlopen`. This avoids maintaining separate module behavior for embedded targets.

### Event bus integration

The module manager wraps each event registration so it can:

- associate it with the owning module;
- increment an in-flight callback count;
- reject callbacks after stopping begins;
- decrement the count after return;
- unregister all listeners during teardown.

Module callbacks must use the existing typed event-bus signatures, including the binary callback type. The module API must not reintroduce unprototyped `bool (*)()` callbacks.

## Application integration for modules

Both applications initialize the module manager after configuration and logging are available but before optional modules are needed.

### rrserver order

Recommended order:

1. configuration, logging, host portability, and event bus;
2. module manager creation and module discovery;
3. core database, radio backend, transport runtime, and listeners;
4. module initialization;
5. core service registration;
6. module start;
7. normal main loop;
8. module stop before transport and event-bus destruction;
9. module unload;
10. remaining application cleanup.

### rrclient order

Recommended order:

1. configuration, logging, host portability, and event bus;
2. module manager creation and discovery;
3. GTK and transport runtime creation;
4. UI and protocol service registration;
5. module initialization and start;
6. autoconnection and normal UI loop;
7. module stop before UI, transport, and event-bus destruction;
8. module unload;

Module configuration should support an ordered list, explicit enable state, module-specific configuration namespace, required versus optional loading, and a configured search path. A required module failure aborts startup cleanly. An optional module failure is logged and startup continues.

## Implementation phases

### Phase 0 Current tree audit and tests

- Re-audit current local `main` and `new_audio` heads plus current `librrprotocol` and `librustyaxe`.
- Build the implementation from `main`; create an explicit reconciliation checklist for overlapping `new_audio` work before editing shared transport or media files.
- Inventory every Mongoose type, function, constant, and utility use.
- Record current HTTP, WebSocket, TLS, MQTT, and media behavior.
- Add protocol-level test vectors and a memory transport backend.
- Establish build and smoke tests for server, GTK client, TUI client, Windows, and embedded configurations that currently work.

Exit condition: current behavior can be tested without relying exclusively on a live Mongoose connection.

### Phase 1 Neutral types and send path

- Introduce opaque runtime, session, channel, request, and buffer types.
- Add backend descriptor and result codes.
- Replace Mongoose pointers in `rrconn_t` with a neutral session pointer.
- Route JSON and binary sends through neutral operations.
- Move WebSocket opcode constants out of protocol APIs and replace them with neutral message kinds.
- Keep Mongoose as the only real backend.

Exit condition: ordinary `librrprotocol` message handlers and send helpers contain no Mongoose calls.

### Phase 2 Receive and lifecycle path

- Translate Mongoose events into neutral events.
- Move client connect, server accept, close, peer address, and connection lookup to the neutral lifecycle.
- Centralize `rrconn_t` creation and destruction around neutral session callbacks.
- Remove Mongoose event values and message structures from public APIs.

Exit condition: `librrprotocol` compiles without including `mongoose.h`.

### Phase 3 HTTP and utility separation

- Introduce neutral HTTP request and response views.
- Convert REST handlers and static routing.
- Replace Mongoose JSON, SHA-1, URL, timer, and subprocess-I/O utility use.
- Isolate MQTT behind a separate service interface if it remains enabled.

Exit condition: Mongoose headers appear only in the Mongoose backend and optional Mongoose-only service implementation.

### Phase 4 Complete librustyaxe modules

- Redesign the descriptor and host API.
- implement validation, lifecycle, event registration tracking, teardown, and portable loading;
- remove the `librrprotocol` dependency;
- add static-module support;
- integrate startup and shutdown into both applications;
- add a minimal test module and lifecycle tests.

Exit condition: both applications can load a harmless module, deliver an event, and shut down cleanly; static-module tests pass without dynamic loading.

### Phase 5 Native WebSocket backend

- Select HTTP/1.1, TLS, and upgrade components.
- Integrate Wslay framing.
- reach feature parity for client and server control plus binary media;
- verify backpressure, fragmentation, close, ping, TLS verification, and static files.

Exit condition: non-Mongoose WebSocket builds pass the same protocol and interoperability suite.

### Phase 6 WebTransport proof and backend

- Build an isolated Picoquic/Picowt interop probe against current Firefox and Chromium.
- Verify current draft compatibility, certificates, Origin validation, streams, datagrams, and clean close.
- Implement the backend using the neutral API.
- add delivery capability negotiation and per-channel selection;
- collect loss, queue, RTT, and drop metrics.

Exit condition: an authenticated session carries reliable control and can switch an audio channel between reliable delivery and datagrams without reconnecting.

### Phase 7 Hardening and release

- fuzz neutral HTTP, JSON, binary-frame, and channel-preface parsers;
- test queue limits and denial-of-service boundaries;
- test packet loss, reordering, path MTU, reconnect, session expiry, and shutdown races;
- document dependency and license combinations;
- package optional backends cleanly;
- retain Mongoose regression builds as a permanent supported configuration.

## Acceptance criteria

The architecture is complete when all of the following are true:

- `rg` finds no Mongoose headers or symbols in `librrprotocol` public headers or protocol implementation files.
- `rrclient` and `rrserver` application logic uses only neutral transport objects.
- Backend conditionals exist only in backend code and build selection.
- The Mongoose backend passes full existing functionality tests.
- The memory backend can drive authentication, chat, CAT, and binary media tests.
- A native WebSocket backend interoperates with the web UI and native client.
- A WebTransport session carries reliable control and unreliable audio concurrently.
- Delivery mode can change per media direction without reconnecting or reauthenticating.
- Backpressure and datagram drops are bounded and observable.
- `librustyaxe` has no dependency on `librrprotocol` for module loading.
- Module descriptors are ABI-validated and callbacks are typed.
- `rrserver` and `rrclient` initialize, start, stop, and unload configured modules in a deterministic order.
- Embedded builds can use Mongoose and static modules without pulling in QUIC, Wslay, or dynamic-loading support.

## Guidance for the implementing model

Do not begin by adding Picoquic or Wslay. Begin with the memory backend and the Mongoose adapter so architectural mistakes are found while behavior is still unchanged.

Do not replace `struct mg_connection *` with `void *` in public structures. That hides coupling without removing it.

Do not make `rrconn_t` the backend object. Preserve a strict ownership boundary between RustyRig session state and transport state.

Do not expose a giant lowest-common-denominator callback containing backend event numbers. Translate into the neutral event set.

Do not force datagrams through a stream-shaped API or streams through a datagram-shaped API. They have different backpressure and lifetime semantics.

Do not modify protocol negotiation and transport abstraction in the same initial patch. First preserve existing WebSocket behavior behind the new boundary, then extend capabilities.

Do not implement hot module unloading until listener ownership and in-flight callback tracking are correct. It is acceptable for the first release to load modules at startup and unload only during orderly shutdown.

## Research references

- Picoquic WebTransport documentation: https://www.privateoctopus.com/picoquic/pico_webtransport.html
- Picoquic repository: https://github.com/private-octopus/picoquic
- Wslay documentation: https://tatsuhiro-t.github.io/wslay/
- LSQUIC repository: https://github.com/litespeedtech/lsquic
- imquic repository: https://github.com/meetecho/imquic
- WebTransport over HTTP/3 draft: https://datatracker.ietf.org/doc/html/draft-ietf-webtrans-http3
- WebTransport API specification: https://www.w3.org/TR/webtransport/
