# Compact wire codec foundation

The first implementation of the replacement RustyRig JSON envelope is in
`librrprotocol/wire.c` and `www/js/webui.wire.js`. It currently covers
object/property messages. It is **not connected to live transports yet**;
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
