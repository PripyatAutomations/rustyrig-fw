# Compact wire codec foundation

The first implementation of the replacement RustyRig JSON envelope is in
`librrprotocol/wire.c` and `www/js/webui.wire.js`. It currently covers
object/property messages plus authentication, hello, keepalives, errors, notices
and alerts. It is **not connected to live transports yet**;
all families must be migrated before the single-format runtime cutover.
There is no old-wire decoder or version fallback in these helpers.

C callers use `rr_wire_encode(dict *)` and `rr_wire_decode(const char *)`.
Returned memory belongs to the caller; NULL means unsupported/invalid input,
an exceeded limit, or allocation failure. JavaScript has corresponding
`rrWireEncode`/`rrWireDecode` functions returning a string/object or null.
The browser helper is intentionally not loaded by index.html until cutover.

## Envelope

```json
{"op":"object.snapshot","request":{"id":"initial-objects"}}
{"op":"property.set","name":"frequency","value":145000000,"target":"00000000-0000-4000-8000-000000000005","request":{"id":"tune-1"}}
```

`op` replaces the redundant family and command wrappers. Family payload fields
move to the root. Target UUIDs and `request`, `stream`, `result`, `inventory`
metadata retain their established structure. Full-range versions and sequence
numbers remain strings; booleans and scalar property values retain their types.

Supported operations:

* `object.snapshot`, `unsubscribe`, `inventory`, `begin`, `descriptor`, `added`,
  `removed`, `end`, `result`, `inventory-entry`, `inventory-end` (all prefixed
  `object.`).
* `property.set`, `descriptor`, `state`, `changed`, `result` (all prefixed
  `property.`).
* `auth.login`, `pass`, `logout`, `challenge`, `authorized`, `error` (all
  prefixed `auth.`); the error operation needs no internal command key.
* `hello`, `ping`, `pong`, `error`, `notice`, `alert`.

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
Version agreement and the remaining message families are not implemented here.

## Tests and next integration

`librrprotocol/tests/wire-vectors.jsonl` supplies both C and JavaScript with
identical internal-event / compact-wire pairs and rejected raw inputs.
`librrprotocol/tests/test_wire.sh` and `www/tests/wire.js` are registered with
the component test runner. Internal dictionaries in those vectors are not a
second supported wire format. Tests compare decoded values/field sets rather
than depending on JSON key order.

Next integration work must add the remaining operation schemas, explicit
single-version agreement, migrate every transport serialization path and both
clients, and remove obsolete aliases/duplicate fields. Replace the runtime
format once, with no compatibility mode. Batching, handles and server-link
routing remain separate changes described in [the audit](wire-protocol-review.md).

## Transport boundaries and future IRC/RTP

Structured dictionary sends and kick notifications now use `ws_send_dict`;
WebSocket frame writes and raw broadcasts use `ws_send_to_cptr`. Dictionaries
are text commands only, never binary media. The frame adapter rejects IRC
connections; the structured send helper already selects IRC translation versus
WebSocket serialization. Staff kick notifications are structured notices.
This is consolidation of the current wire send paths, not the compact cutover.

Keep future IRC/DCC AUDIO control translation in librrprotocol and media/session
policy in rrserver. An IRC adapter should translate commands into the same
validated requests; an RTP adapter should carry media for authorized stream
sessions. Do not route binary media through a chat-command dictionary or infer
RTP authorization from an IRC nick. Connection/session/room membership, codec
selection and TX ownership still need explicit mapping. No DCC/RTP handshake,
media transport, authentication or port configuration is implemented here.

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
