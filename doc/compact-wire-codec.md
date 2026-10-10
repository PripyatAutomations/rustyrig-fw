# Compact RustyRig wire protocol

The live C/native/browser WebSocket transports use the compact codec in
`librrprotocol/wire.c` and `www/js/webui.wire.js`. The handshake requires the
single WebSocket subprotocol `rustyrig.v1`; missing or different versions receive
HTTP 426. Native clients check the server's selected version before login.
There is no old-envelope decoder, alias negotiation or version fallback.

`librrprotocol/wire-schema.json` is the authoritative operation/field registry.
`librrprotocol/tests/generate_wire.py` generates the matching C and browser tables;
`--check` detects stale copies. All current live message families use this boundary.

C callers use `rr_wire_encode(dict *)` and `rr_wire_decode(const char *)`.
Returned memory belongs to the caller; NULL means unsupported/invalid input,
an exceeded limit, or allocation failure. JavaScript has corresponding
`rrWireEncode`/`rrWireDecode` functions returning a string/object or null.
The browser loads the registry and codec before its application handlers.
Use `rrSendMessage` for commands and `rrSendBinary` for media.

## Envelope

```json
{"op":"object.snapshot","request":{"id":"initial-objects"}}
{"op":"property.set","name":"frequency","value":145000000,"target":"00000000-0000-4000-8000-000000000005","request":{"id":"tune-1"}}
```

`op` replaces the redundant family and command wrappers. Family payload fields
move to the root. Target UUIDs and `request`, `stream`, `result`, `inventory`
metadata retain their established structure. Full-range versions and sequence
numbers remain strings; booleans and scalar property values retain their types.

Operations include object/property discovery and control, authentication, CAT,
chat/rooms/accounts, media subscription and codec negotiation, serial tunnelling,
hello, keepalive, errors/notices/alerts and callsign lookup. See the registry for
the complete allowlist and field mapping. Chat's extra room metadata uses
`room-info`; its media metadata uses `media-info`, avoiding collisions with
root payload fields such as `room`.

Message-level `msg.ts` becomes `time`. Family payload timestamps remain `ts`
where they have their own meaning (for example authentication challenge time).
Keepalive `ping.ts` becomes `echo` in both ping and pong; it remains a monotonic
microsecond timestamp, separate from wall-clock `time`. Message text fields
become `text`, so old nested `msg` or `cmd` fields cannot become ambiguous.
These mappings preserve the existing event meaning without timestamp guessing.

The codec transforms between compact wire JSON and application event
*dictionaries*. Keeping existing in-process event keys does not make those
keys a supported wire protocol. The new decoder rejects the old JSON envelope,
as well as mixtures of new and old fields. Generic dictionary serialization
and the event bus are unchanged.

## Boundary validation

The initial codec checks envelope operations and field paths, primitive types,
finite numbers, browser-safe integer values, the 65,535-byte JSON limit,
malformed/duplicate JSON fields, and ambiguous dotted/escaped field names.
Field names must be unescaped ASCII identifiers starting with a lowercase
letter and containing lowercase letters, digits, `_` or `-`, at most 63 bytes.
Arrays and empty containers are not part of this initial scalar schema.
JSON string values may contain escaped punctuation and Unicode; the shared
vectors cover those without confusing them with envelope keys.

This is structural encoding validation, **not** authorization or complete
operation-schema validation. The current object/property validators and owner
policy must still validate UUIDs, request IDs, descriptors, state invariants,
request direction, backend value ranges and permissions after decoding.
The helper deliberately does not add connection state or UI behavior.

## Tests and future work

`librrprotocol/tests/wire-vectors.jsonl` supplies both C and JavaScript with
identical internal-event / compact-wire pairs and rejected raw inputs.
`librrprotocol/tests/test_wire.sh` and `www/tests/wire.js` are registered with
the component test runner. Internal dictionaries in those vectors are not a
second supported wire format. Tests compare decoded values/field sets rather
than depending on JSON key order.

The cutover replaces the live runtime format once, with no compatibility mode.
Shared vectors cover all registered operations and representative fields;
production socket fixtures exercise authentication, radio state, rooms, media,
serial and object discovery. Batching, handles and server-link routing remain
separate changes described in [the audit](wire-protocol-review.md).

## Transport boundaries and future IRC/RTP

Structured dictionary sends and kick notifications now use `ws_send_dict`;
WebSocket frame writes and raw broadcasts use `ws_send_to_cptr`. Dictionaries
are text commands only, never binary media. The frame adapter rejects IRC
connections; the structured send helper already selects IRC translation versus
WebSocket serialization. Staff kick notifications are structured notices.
Both receive paths decode the new format before semantic dispatch.

Keep future IRC/DCC AUDIO control translation in librrprotocol and media/session
policy in rrserver. An IRC adapter should translate commands into the same
validated requests; an RTP adapter should carry media for authorized stream
sessions. Do not route binary media through a chat-command dictionary or infer
RTP authorization from an IRC nick. Connection/session/room membership, codec
selection and TX ownership still need explicit mapping. No DCC/RTP handshake,
media transport, authentication or port configuration is implemented here.

## Pressure, encoder hints and latency

Slow connections stay open. Realtime audio has an 8 KiB application send-queue
budget and video 256 KiB; new frames are skipped when that budget is full. The
adapter never deletes arbitrary bytes from a partially written WebSocket frame.
Reliable traffic has a 1 MiB budget and an explicit failed enqueue result plus
bounded diagnostics. No command is retained for replay after reconnect.

`media.quality-hint` is a local event, not a network operation. Queue pressure
lowers the normalized hint to 75 or 50 percent, with five-second recovery
hysteresis. Native TX encoders consume hints; a shared server audio encoder uses
the minimum hint among that channel's active subscribers. The DSP manager sends
nonblocking, deduplicated quality IPC over atomic Unix control messages. Name an encoder `rr-encoder` to opt in.
The worker prefers a writable, runtime-mutable `quality` property, then `bitrate`.
Hints reduce quality toward the property's worst bound, or scale bitrate against
its initial baseline; 100 restores the original setting. LAME's reversed quality
scale is handled separately. Custom encoder quality scales must follow this
higher-is-better contract or need an explicit adapter.

The shipped Opus pipelines use VBR. Vorbis pipelines already use quality-based
VBR; this host's Vorbis plugin does not mark its properties mutable in PLAYING,
so live hints do not restart it or corrupt stream/container headers. Unsupported
encoders continue with their configured settings. The control helper checks
property types, ranges and runtime mutability rather than assuming every codec
supports the same knobs.

RTT is measured on a local monotonic clock. Keepalives echo microsecond timing;
normal requests also use their existing request IDs for one bounded outstanding
RTT sample per connection. The first matching reply measures network round trip
plus peer processing, not remote wall-clock offset or one-way delay. No NTP or
extra per-message timing bytes are required. RTT and queue pressure are distinct
signals: a high-latency link need not have low bandwidth.

A receipt acknowledgement is not evidence of an applied hardware value. CAT
receipt acknowledgements no longer duplicate frequency/mode/width as state; displays
use backend observations. Generic control results and availability retain their
explicit semantics. Unavailable reads clear live values, while the object cache
can retain a separate last-known observation. Disconnect clears native VFO state
and browser connection/PTT state. Browser playback cancels stale scheduled
sources, bounds its jitter backlog and starts fresh after reconnect.

The optional [WWV worker plan](wwv-time-decoder-plan.md) is deferred and not a
latency prerequisite.

## Dictionary cost measurement

`librustyaxe/tests/bench_dict.c` is a manual benchmark with compile/run commands
in its header; it uses the project's monotonic clock and has no timing test
thresholds. On this development host one run measured existing-key lookup at
30 ns, missing-key lookup at 26 ns, build/free of 20 fields at 2.1 us, serialization
at 5.7 us and parsing/free at 12.0 us. These are warm synthetic measurements,
not a production CPU profile or a comparison of hash algorithms.

The current dictionary uses fixed-seed MurmurHash and open addressing. Keep
that algorithm for now and measure complete protocol workloads before changing
it. Its unaligned integer read was replaced with memcpy, preserving native
hash bytes while avoiding alignment/type-aliasing violations. The alignment
sanitizer reproduced the old failure; the offset-key regression passes after
this change. Hardware-specific acceleration is not required for this fix.
