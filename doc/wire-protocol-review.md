# Wire protocol review — 2026-10-10

Status: audit and proposed design, not an implemented replacement protocol.
The current C implementation is authoritative. This review covers RustyRig
WebSocket control and binary media, not conventional IRC wire syntax.

## Recommendation

Keep readable JSON for commands, results, discovery and state. Use a single
flat envelope with a dotted operation name, batch related observations, and
avoid retransmitting stable descriptors. Keep bulk media binary. Define one
supported protocol version and reject mismatches; do not implement legacy
fallback, aliases or a second decoder. When the replacement ships, remove the
old wire format from C, browser code, tests and documentation together.

Compactness must preserve explicit target identity, authoritative ownership,
unknown/unavailable state, correlation, ordering and access checks. A short
message that depends on the wrong session or loses state after reconnect is
not an improvement.

## Current implementation and consumers

| Area | Authoritative code | Consumers / implications |
| --- | --- | --- |
| JSON serialization | `librustyaxe/json.c:dict2json/json2dict` | Dotted dictionary paths become nested JSON; output is already compact. Generic serializer also serves configuration/events. |
| Sending | `librrprotocol/srv.send.c:ws_send_dict` | Some senders serialize directly (`cli.main.c`, `cli.rigctl.c`); broadcasts serialize once per recipient. A wire codec must cover every transport send path. |
| Text dispatch | `librrprotocol/srv.http.c`, `cli.main.c` | `msg.type` selects family, family `.cmd` selects operation. Authentication gating precedes server dispatch. |
| Object/property model | `librrprotocol/objects.c`, `rrserver/objects.c` | `rrclient/objects*.c`, `www/js/webui.objects.js`; schema and state are distinct, versions/cursors are uint64 decimal strings. |
| CAT/PTT | `librrprotocol/srv.rigctl.c`, `ws.cat.c` | Native VFO/event handlers and `www/js/webui.rigctl.js`; echoed controls and actual observations must remain distinct. |
| Chat and accounts | `librrprotocol/srv.chat.c`, `cli.chat.c` | Native chat/event handlers and `www/js/webui.chat.js`; room membership and account checks remain server policy. |
| Media/serial/file | `ws.mediachan.c`, `ws.serial.c`, `ws.file-xfer.c`, `binframe.c` | Native media/serial and browser media/framing/file handlers; not every legacy path uses the binary format. |
| Authentication/hello | `srv.auth.c`, `ws.auth.c`, `auth.hash.c` | Native and browser auth; current hello reports software/hardware/role, not a strict protocol-version agreement. |

See [object/property semantics](object-property-protocol.md),
[media framing](media-frames.md), [parity map](client-parity.md) and
[existing input-validation audit](transport-security-audit.md).
`doc/NEWPROTO` is an old command sketch, not the implemented specification.
The media document calls its layout “binframe v2”, but the actual version byte
is `0x01`; a replacement spec must use unambiguous names and version numbers.

There is no completed federation protocol in the inspected dispatchers.
Existing client authentication, session tokens and room broadcasts do not
establish a trustworthy server-to-server authority model.

## Measured overhead

Run `python3 librrprotocol/tests/wire-size-audit.py`. Object samples come
from actual documented current JSON; CAT uses representative fields from the
current frequency broadcast. Proposed samples preserve the illustrated state
flags and uint64 strings. These are payload sizes, not traffic captures or a
claim about total bandwidth saved. WebSocket, TLS and network headers are
excluded; each current message is counted separately without array overhead.

| Sample | Current bytes | Proposed bytes | Reduction |
| --- | ---: | ---: | ---: |
| Object snapshot request | 88 | 59 | 33.0% |
| Node descriptor | 262 | 233 | 11.1% |
| Property descriptor | 333 | 302 | 9.3% |
| Property change, UUID/epoch retained | 287 | 256 | 10.8% |
| Property change, explicit target/epoch bindings | 287 | 172 | 40.1% |
| Four property changes sharing a cursor | 1148 | 758 | 34.0% |
| Sixteen property changes sharing a cursor | 4592 | 2726 | 40.6% |
| Frequency echo, duplicate value removed | 143 | 90 | 37.1% |

Binding savings exclude binding setup messages. Batch measurements repeat a
representative property at different targets: they show structural savings,
not a frozen snapshot guarantee. A batch can share a cursor only if all items
really have the same cursor; otherwise retain per-item ordering metadata.
Descriptor/schema traffic is mostly startup traffic. Reducing frequent state
and media traffic matters more than shortening rare login messages.

## Proposed readable envelope

Replace `msg.type` plus `<family>.cmd` with one `op`, lift operation fields to
the root, and use one spelling for each concept. Examples:

```json
{"op":"object.snapshot","request":{"id":"initial-objects"}}
{"op":"property.changed","name":"frequency","type":"integer","observed":true,"known":true,"available":true,"version":"7","value":145000000,"target":"00000000-0000-4000-8000-000000000005","stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"13"}}
{"op":"cat.freq","room":"#rig1","freq":145000000,"user":"alice","vfo":"A","ts":1791580800}
```

These are illustrative mappings, not the full replacement schema. Keep words
such as `target`, `value` and `version`, rather than one-letter keys or numeric
operation codes. Document every operation's direction, authenticated peer
role, required/optional fields, types, limits, errors and side effects.
Use strict lowercase operation names and remove historical spelling aliases.
Use one timestamp location and unit, with explicit semantics for absent time.
Keep full-range uint64 counters as canonical decimal strings for JavaScript;
keep frequency in integer Hz and do not substitute floats for counters.

A CAT frequency echo currently contains both `cat.freq` and `cat.state.freq`.
Choose one field in the new schema and migrate all readers. Do not merge
request success with observed hardware state: a correlated result acknowledges
the request; an observation reports actual state. Selection acknowledgments
must also remain distinct from physical active-VFO observations.

Avoid repeated authentication tokens on ordinary commands once the connection
is authenticated. Check each token's purpose before removal: current private
PART confirmation uses a secret token to distinguish sessions. Replace that
with a non-secret session identifier, never with an account name alone.
Do not change password hashing as a side effect of compaction; separately
specify client and peer authentication before making any auth-wire cutover.

## Batch and binding rules

Start with batching object/property snapshot data and related state changes.
Batch by real cursor/target scope and bound count/bytes/time, preserving order.
Do not delay PTT release or command results behind a large snapshot. A batch
must validate fully before applying anything, or explicitly define per-item
failure semantics; do not accidentally introduce partial state mutation.
Snapshot begin/end, live interleaving and per-property versions retain the
current convergent-snapshot semantics.

Link-local numeric target handles are a second step. Introduce explicit
UUID-to-handle bindings before use; keep UUIDs canonical in persistent state.
Reset bindings on reconnect and epoch reset, never recycle handles within a
link epoch, reject unknown/stale handles and cap the binding table. A forwarding
server translates handles for each outgoing link. It must not forward a
link-local ID as if it were a global object identity.
Likewise, moving epoch out of every update requires an explicit stream-open
state, a stream identifier if several origins share a link, and rejection of
updates outside the established epoch. No inference from the last random
message. Descriptors remain necessary for new subscribers and schema changes.

## Binary media

The existing header is 28 bytes and includes codec, rig, VFO, stream, sequence,
length and timestamp. At 50 frames/s that is 1,400 header bytes/s. A hypothetical
16-byte header saves 600 bytes/s, before negotiation overhead. For small audio
payloads this may matter more than JSON key lengths; for video it usually does
not. Measure codecs/frame duration before choosing a replacement layout.

Use an explicit stream-open descriptor to bind codec, rig, VFO and direction,
then a small per-frame stream ID/sequence/timestamp header. Keep exact length
validation, defined byte order, counter-wrap handling and maximum payloads.
Do not silently remove timestamps or conflate media timing with wall-clock
chat time. Stream IDs need enough space for media plus serial/file traffic;
current serial IDs are deliberately not recycled within a connection.
WebSocket length can replace an inner payload length only when exactly one
application frame occupies a WebSocket message. A future byte-stream transport
still needs length framing. Do not replace the current binary header until
all media producers, fwdsp IPC consumers and both client parsers are inventoried.

Compression is an optional later measurement, not a substitute for a clean
schema. It adds memory/CPU, negotiation and decompression-limit requirements.
No external codec library is needed for the initial readable-JSON changes.

## Robustness for both client and server links

* Require an explicit supported protocol version before operational messages.
  Fail with a bounded diagnostic on mismatch; no legacy probing or fallback.
  Distinguish protocol version from optional supported features.
* Validate one UTF-8 JSON object per text frame. Reject duplicate/ambiguous
  paths, wrong types, invalid numbers and unsupported operations. Specify
  extension-field handling per schema so typos cannot become silent defaults.
  Retain existing NUL, depth and exact-length checks; add operation limits.
* Specify limits for fields, collection counts, outstanding requests, bindings,
  snapshots, queued output and peer fanout. Current 65,535-byte application
  message limits alone do not bound cumulative queue growth.
* Correlate mutating requests and results. Define duplicate-ID handling and
  idempotent retries; do not replay PTT key-down automatically after reconnect.
  Deadlines/disconnects must release transmit ownership through existing policy.
* Authenticate server links separately from ordinary user sessions. A declared
  hello role is not authentication or authorization. Bind each accepted origin
  to configured trust and permitted objects/rooms/operations.
* Use stable origin identity plus origin epoch and sequence for replicated
  events. Preserve origin metadata through forwarding, deduplicate with bounded
  storage, limit hops and never infer order across independent origin streams.
  Keep local-link sequence/handles separate from origin event identity.
* Assign an authoritative owner for each object/property and physical TX lease.
  A peer must not become authorized by supplying a `user`, `origin` or `target`
  field. Route commands to the owner and return its correlated result.
  Define partition behavior: stale observations are marked unavailable and
  remote control fails closed; no competing transmitter ownership.
* Define reconnect/resync boundaries and schema refresh. Reuse convergent
  snapshots and property versions where appropriate; specify gap detection and
  replay retention before promising reliable federation delivery.

## Implementation order and deletion plan

1. Inventory all operations and publish machine-readable schemas plus golden
   examples, including directional authorization and negative cases. Record
   actual startup/idle/tuning/chat/media traffic sizes. This review's samples
   are a starting point, not a complete operation registry.
2. Implement one protocol encoder/decoder boundary in `librrprotocol`, mapping
   the new wire schema to internal events. Keep generic dictionaries/event
   names separate from wire layout; do not modify generic JSON serialization
   just to rename protocol fields. Replace every direct transport serializer.
3. Cut over server, GTK/TUI common client and WebUI together. Remove old wire
   parsers, aliases, duplicate fields, obsolete fixtures and documentation in
   the same change. Retain no compatibility option. Test mismatch rejection.
4. Add bounded snapshot/state batching; then consider negotiated target/stream
   bindings only if traffic measurements justify their state complexity.
5. Specify peer trust/authority and resync, then implement server-link routing.
   Reuse the same operations, with server-only routing metadata and permissions.
   Ordinary clients cannot submit trusted replication events.
6. Change binary framing only after measuring media overhead and updating all
   producers/consumers atomically. Do not add an old binary-header decoder.

Required validation for a wire cutover: golden C/JS round trips, malformed and
oversized messages, wrong roles/targets, duplicate requests, reconnect/epoch
changes, interleaved snapshots, stale handles, binary wrap/truncation, peer
loops and partitions, and transmit-owner cleanup. Preserve non-GTK builds.

## Audit validation

Existing librrprotocol tests initially failed because the filesystem/network
sandbox denied IRC socket operations. Rerunning with loopback network access
passed all suites. No runtime wire behavior changes are made by this review.
