# UUID object/property protocol (Phase 4)

Status: implemented on `multirig`, alongside the legacy protocol. This is
pre-1.0 protocol, not a version-negotiation framework. Phase 5 is not started.

## Ownership and implementation

`librrprotocol/objects.{h,c}` owns wire scalar/sequence validation and event
dispatch. Existing WebSocket dispatch routes authenticated requests to
`protocol.object.request`; incoming client messages emit
`protocol.object.message`. Programs register handlers with `event_on()`.
`rrserver/objects.c` owns discovery, subscriptions, controls, and model event
translation. It resolves the existing registry and property stores; it does
not maintain a second authoritative value store.

Canonical addresses are **object UUID + local property name**. `rig0`, `A`,
and backend names are metadata, never network lookup keys. `vfo.A.frequency`
is explicitly rejected as a rig property address in this protocol. The graph
is node -> rig -> VFO. Backend instances are not protocol objects.

## Messages and serialization

Messages use existing `dict_value_t`, `dict2json()` and `json2dict()`. Dotted
dictionary keys serialize as nested JSON, as in the examples below. No new
value container or encoding is introduced.

| `msg.type` | `object.cmd` / `property.cmd` | Direction |
| --- | --- | --- |
| object | snapshot | Client requests discovery and subscribes |
| object | unsubscribe | Client cancels subscription/snapshot |
| object | begin, descriptor, end | Server snapshot boundaries/objects |
| object | added, removed | Server live object lifecycle |
| object | result | Server request error or unsubscribe acknowledgment |
| property | descriptor, state | Server schema/current readable state |
| property | changed | Server authoritative observation/availability change |
| property | set | Client control request |
| property | result | Server correlated control result |

Requests require a nonempty `request.id` string, at most 64 bytes. Snapshot
messages echo it, including begin/end and each property message. Live events
omit it. All server messages include `stream.epoch` (runtime UUID) and
`stream.seq` (unsigned 64-bit decimal string). Unknown commands return
`invalid-request`; unauthenticated connections cannot enter this dispatcher.

```json
{"msg":{"type":"object"},"object":{"cmd":"snapshot"},"request":{"id":"initial-objects"}}
{"msg":{"type":"object"},"object":{"cmd":"begin"},"request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
{"msg":{"type":"object"},"object":{"cmd":"descriptor","uuid":"00000000-0000-4000-8000-000000000001","type":"node","alias":"station","lifecycle":"persistent"},"request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
{"msg":{"type":"object"},"object":{"cmd":"descriptor","uuid":"00000000-0000-4000-8000-000000000003","type":"rig","owner":"00000000-0000-4000-8000-000000000001","alias":"rig1","name":"Hamlib radio","backend":"hamlib","lifecycle":"persistent"},"request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
{"msg":{"type":"object"},"object":{"cmd":"descriptor","uuid":"00000000-0000-4000-8000-000000000005","type":"vfo","owner":"00000000-0000-4000-8000-000000000003","alias":"A","name":"A","lifecycle":"persistent"},"request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
```

These are concrete examples of the implemented serialization shape; UUIDs
are illustrative, not an installed server's identities. Object descriptors
carry explicit `type` (`node`, `rig`, `vfo`), `uuid`, owner UUID except on the
node, alias, optional display `name`, and `lifecycle` (`persistent` or
`ephemeral`). A rig also supplies informational `backend`. Native VFO IDs
and backend pointers are not exposed. Clients must tolerate child-before-owner
delivery and determine controls from descriptors, not object/backend type.

Property descriptors require target UUID, name, type, readable and writable.
Optional fields: unit, minimum, maximum, step, enum. Integers use JSON numbers
within +/-9007199254740991 (and the actual backend C type's narrower range);
other scalar types are `string`, `boolean`, and finite `number`. Enum is a
space-separated list of nonempty string tokens (no spaces within a choice).
Numeric constraints use the property's type; integer step is anchored at the
minimum or zero. Non-integer step grids are not supported. Names are 1-63
ASCII alphanumeric/dot/underscore/hyphen characters. UUIDs use lowercase
36-character hex/hyphen form. Sequences and versions use strings so browsers
retain the entire uint64 range.

```json
{"msg":{"type":"property"},"property":{"cmd":"descriptor","name":"frequency","type":"integer","readable":true,"writable":true,"unit":"Hz","minimum":1,"maximum":2147483647,"step":1},"target":"00000000-0000-4000-8000-000000000005","request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
{"msg":{"type":"property"},"property":{"cmd":"state","name":"frequency","type":"integer","observed":false,"known":false,"available":false,"version":"0"},"target":"00000000-0000-4000-8000-000000000005","request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
{"msg":{"type":"object"},"object":{"cmd":"end"},"request":{"id":"initial-objects"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"12"}}
{"msg":{"type":"property"},"property":{"cmd":"changed","name":"frequency","type":"integer","observed":true,"known":true,"available":true,"version":"7","value":145000000},"target":"00000000-0000-4000-8000-000000000005","stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"13"}}
{"msg":{"type":"property"},"property":{"cmd":"changed","name":"frequency","type":"integer","observed":true,"known":true,"available":false,"version":"8","value":145000000},"target":"00000000-0000-4000-8000-000000000005","stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"14"}}
```

Schema and state are separate messages/stores. Unknown has no `value`.
Unavailable is a boolean state, never a fabricated zero/null. A failed first
observation can be observed=true, known=false, available=false. A previously
known value survives temporary unavailability. `available=true` requires
known=true; known implies observed. Non-readable properties have descriptors
but no emitted state. Descriptors currently advertise width as read-only:
neither backend's generic control handler implements width SET yet.

## Snapshot consistency and lifecycle

The server subscribes the connection before begin, captures UUIDs (not
borrowed object pointers), and serializes at most four objects per event-loop
iteration. Polling can run between batches. Each object/property is read at
serialization time. This is a convergent snapshot, not a frozen simultaneous
sample of every radio. End means the captured graph was traversed, together
with intervening lifecycle/property events. Removed queued objects are
skipped; newly added objects are delivered live.

`stream.seq` increments on model property/lifecycle/schema events, including
when no client is subscribed. Multiple descriptors/states from a single
snapshot or lifecycle event may share a sequence. Epoch changes when the
server's protocol service restarts. The existing canonical property version
changes on observation/availability changes, not unchanged polls. Versions
are compared per target UUID/property, not across rigs.

A matching begin resets the cache for the new snapshot/epoch; end marks it
ready. Within that stream, older object/descriptor sequence numbers are
ignored. State requires a newer property version and a non-older sequence;
duplicate state is ignored. Thus an old snapshot value cannot overwrite a
newer live observation. Results never change cached state. Disconnect clears
the common client cache; reconnection requests a new snapshot.

`object.added` has descriptor shape and is followed by property descriptors
and states. `object.removed` carries `object.uuid`; removing an owner removes
its known descendants. Tombstones retain owner/sequence metadata to prevent
late snapshot resurrection. A strictly newer add can recreate an object;
ephemeral instances should normally use new UUIDs. Schema additions emit an
object descriptor plus current property schema/state. In-place schema
replacement and property removal are not implemented.

Subscriptions currently cover all local objects for an authenticated client,
until unsubscribe/connection close. That is Phase 4 policy, not an immutable
broadcast contract. Discovery caps at 128 rigs and 4095 non-node objects;
client caches cap at 4096 entries including tombstones and 256 properties per
object. Heavy future ephemeral churn requires resnapshotting; per-property
serialization budgets, backpressure and filtered subscriptions remain work
for a future phase. Normal configured A/B rigs are far below these bounds.

## Controls

```json
{"msg":{"type":"property"},"property":{"cmd":"set","name":"frequency","value":7100000},"target":"00000000-0000-4000-8000-000000000005","request":{"id":"tune-1"}}
{"msg":{"type":"property"},"property":{"cmd":"result"},"request":{"id":"tune-1"},"result":{"code":"ok"},"stream":{"epoch":"00000000-0000-4000-8000-000000000099","seq":"15"}}
```

Server resolves UUID -> property -> writable/type/constraints ->
`rr_rig_control()` -> that rig's handler. Existing station write permission,
mute and noob/elmer gates apply; this is not new per-object ACL machinery.
Observed-but-unavailable targets return `unavailable`. Results distinguish
`ok`, `invalid-request`, `unknown-object`, `unknown-property`, `read-only`,
`forbidden`, `invalid-value`, `unavailable`, `unsupported`, `backend-failure`.
Object snapshot requests can additionally return `too-large`.

`ok` means backend acceptance under current semantics, not an observation.
Neither canonical server state nor the client cache is optimistically changed
by the generic path. The next observation/event establishes actual state.
No raw Hamlib error codes are part of this contract.

A never-observed property does not by itself prove a backend is offline.
Its SET may reach the backend and return `backend-failure` if the endpoint
has never connected. After an observed availability loss, the explicit
unavailable state permits rejection as `unavailable` before backend dispatch.

## Persistence and cache API

Node UUID is stored in the existing rig identity table under reserved alias
`@node`, scoped by the existing identity namespace. Config aliases cannot
use `@`. Rig UUID storage and VFO UUIDs scoped by rig/native ID are unchanged.
Missing SQLite, lookup/create failure, or malformed stored UUID now aborts
configured-object startup and tears down partial runtime construction.
There is no generated fallback for persistent objects. Explicit ephemeral
VFOs may still use generated UUIDs. Database identity rows successfully
created before a later startup failure remain reusable on retry.

`rrclient/objects.h` exposes new/free, apply, ready, count, object(UUID),
property(UUID, name, descriptor_boolean), and dump(callback). The cache owns
deep dictionary copies; lookup results are borrowed until the next apply or
free. Metadata retains owner UUID even if that owner has not arrived yet.
Aliases are not keys. `objects.events.c` handles connection events and
automatic discovery; `/object` prints the cache through the existing common
UI output. It works alongside GTK/TUI, not inside a frontend-specific layer.
Browser `js/webui.objects.js` mirrors the cache with Maps and BigInt counters;
`rrObjectsDump()` provides a console diagnostic. Existing widgets stay legacy.

## Validation and stopping boundary

New component tests are discovered by the existing component runner:

```sh
make -j4
./tests/run-tests.sh
bash librrprotocol/tests/test_objects.sh
bash rrclient/tests/test_objects.sh
bash rrclient/tests/test_objects_browser.sh
bash rrserver/tests/test_multirig.sh
bash rrserver/tests/test_object_protocol.sh
```

The last command starts private dummy rigctld and a loopback WebSocket
fixture with the production registry/backends, object dispatcher, serializer,
and native cache. Authentication is pre-set in this fixture; auth gating is
tested separately. It does not start the GTK/TUI application or validate a
physical FT-891. No installed server/configuration/database is changed.

Live acceptance: node + rig0/internal A/B + rig1/Hamlib A/B appear together
in the cache. Hamlib observations are A=145000000 FM, B=146000000 FM. SET to
rig1/A reaches Hamlib; acceptance leaves the cache unchanged, then polling
updates it to 7100000. rig0/default and legacy broadcasts remain isolated.
The test restores 145000000, checks errors/lifecycle, disconnect/reconnect,
missing endpoint, and UUID persistence across process restarts.

Remaining migration debt: GTK/TUI/CAT still use default-only compatibility,
legacy controls retain their previous optimistic behavior, RX/TX selector
property values still use their existing alias semantics, Hamlib I/O remains
synchronous, and installed-process/physical-radio behavior is not proven by
dummy tests. No room/media/ACL/lease/SDR/federation changes were made.

Recommendation for Phase 5: store room-slot -> persistent rig UUID mappings,
resolve through the registry, and treat a missing target as unavailable,
never silently retarget by alias/default. Resolve VFOs by owned UUID when a
slot needs a VFO; define selector value semantics explicitly before clients
start treating existing alias-valued RX/TX properties as UUID references.
Review this phase before implementing any mapping or CAT migration.
