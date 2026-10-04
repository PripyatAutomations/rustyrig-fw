# Rig property migration plan

Status: Phase 3 implemented through first-class VFO ownership and named
multi-rig configuration; awaiting review before discovery protocol, room,
CAT-client, media, or later migration work.

This document records the architecture investigation and the agreed stopping
point for introducing a backend-neutral rig property layer. It is intended to
be sufficient context for resuming the work without relying on chat history.

## Goal

Introduce a server-owned property layer that can represent rig and VFO state,
initially backed by Hamlib, without creating a second generic value system.
Each property must belong to an explicit runtime rig instance from the start;
the property service must not become another singleton for "the radio." The
first implementation milestone ends after Hamlib is using the property layer
and its existing FT-891 behavior has been preserved and tested.

Room/VFO binding resolution and native CAT consumers must not start depending
on the new layer until that milestone has been reviewed.

Do not prescribe concrete `rr_property_t` or `rr_rig_t` structures before the
implementation pass has examined the ownership and lifetime requirements of
the existing dictionary APIs. Reuse existing project abstractions wherever
they fit.

## Compatibility is migration scaffolding

RustyRig is pre-1.0. Preserving current behavior during Phase 1 is useful for
making small, testable changes, but the new property model must not be shaped
around the old wire or snapshot representations:

- `cat.state` is a temporary output adapter for existing clients.
- `rr_vfo_data_t` is a temporary compatibility snapshot.
- `vfos[]` is a temporary compatibility view/cache.
- Existing `rigctl` messages may remain as a temporary input adapter.

The intended read direction is:

    backend observation
      -> generic rig/property state
           -> future generic property events/protocol
           -> temporary rr_vfo_data_t/vfos[] adapter
           -> temporary cat.state adapter

The intended write direction is:

    old rigctl request
      -> temporary input adapter
      -> generic control request
      -> backend

Later GTK, TUI, CAT, and WebUI work will consume the generic model and allow
the adapters to be removed. They are deliberately not part of Phase 1.

## Rig ownership invariant

Every generic property belongs to an explicit rig instance. Phase 1 does not
need multiple configured physical radios, room mappings, or final public rig
identity semantics, but it must introduce enough runtime identity and
ownership for the current FT-891 state to belong to one rig object.

Property APIs must take an explicit rig pointer, handle, or ID. Do not add an
implicit singleton API such as `property_get("vfo.A.frequency")`.

The first property implementation must prove this boundary by constructing two
independent fake rigs with independent property sets. VFOs must be represented
dynamically by identifier rather than fixed A/B members; A-Z and future SDR
identifiers must remain possible.

## Phase 0 findings

### Existing value and state containers

- `librustyaxe/dict.h` already defines `val_type_t`, `dict_value_t`, typed
  getters/adders, and `dict_enumerate_typed()`. It supports null, string,
  integer, floating-point, boolean, character, and pointer values. A property
  layer should reuse this typed value representation instead of adding another
  tagged union.
- `librrprotocol/vfo.h` defines `rr_vfo_data_t`, `vfos[MAX_VFOS]`, and
  `active_vfo`. This is the current merged server-side compatibility snapshot
  for frequency, mode, width, power, and related per-VFO fields.
- `rrserver/globalstate.h` holds server-wide rig state and the selected
  `rr_backend`, but it is not a generic property store.
- `rrserver/backend.hamlib.h` has `hl_state[MAX_VFOS]`, a Hamlib-specific cache
  containing Hamlib-native values and several fields duplicated in `vfos[]`.
- `rrserver/backend.internal.c` keeps a separate private per-VFO state cache.
- `rrclient/vfo.c` keeps client state in a typed dictionary using keys of the
  form `vfo.<ID>.cat.state.*`. Client UI and CAT implementations consume this
  state through the `vfo_state_get_*()` accessors.
- `librrprotocol/srv.rigctl.c` contains an older private `ws_rig_state_t`
  implementation marked with a merge TODO. Its state getter/poll/send helpers
  have no source callers outside that file and must not be treated as the new
  authoritative state container.

### Backend seam

`rrserver/backend.h` defines the current backend function table. It covers
lifecycle, polling, VFO support, PTT, tuner/split behavior, power, mode,
frequency, width, capabilities, and sending current state to a client.

`rrserver/backend.c` is already the backend-neutral policy layer:

- `rr_freq_set()`, `rr_set_mode()`, `rr_set_width()`, and related wrappers call
  the selected backend and update the merged `vfos[]` snapshot.
- `rr_be_merge_poll()` merges partial backend observations while preserving the
  last known frequency, mode, or width when a rig cannot read a field.
- `rr_be_poll()` seeds an unprobed inactive VFO from the active VFO before its
  first poll. This is required for radios such as the FT-891 that cannot answer
  every query for an inactive VFO.
- `rr_be_vfo_supported()` combines backend capability reporting with the
  configured `rig.vfos` limit.
- `rr_cat_state_send()` currently delegates initial-state serialization back
  to the selected backend.

This makes `rrserver/backend.c`, or a narrowly scoped server component beside
it, the intended property-layer seam. Rig hardware policy does not belong in
`librrprotocol`.

### Hamlib path

`rrserver/backend.hamlib.c` currently owns several distinct responsibilities:

1. Hamlib initialization, reconnection, VFO conversion, device reads, and
   device writes.
2. A Hamlib-native per-VFO cache.
3. Conversion of poll results into heap-allocated `rr_vfo_data_t` snapshots.
4. Construction of wire-facing `cat.state` dictionaries.
5. State comparison, rate limiting, broadcast, and initial-state delivery.

`hl_poll()` selects the Hamlib VFO and reads frequency, mode/width, PTT, and
signal-related values where the rig supports them. It deliberately tolerates
FT-891 failures for inactive-VFO mode and width reads.

`hl_send_state_to()` and the `last_state_dict[]`/`last_state_send[]` data are
protocol-publication concerns and should move behind the neutral server seam.
Hamlib should ultimately retain only device integration, conversion, reconnect
behavior, and hardware-specific quirks.

### Internal backend duplication

`rrserver/backend.internal.c` independently implements its own `cat.state`
dictionary creation, comparison keys, previous-state dictionaries, rate
limiting, broadcasting, and initial-state sending. Its implementation contains
parity comments pointing at the Hamlib implementation.

The duplication confirms that publication belongs in a common server layer.
The first milestone must preserve the internal backend, but does not need to
fully migrate or clean it up before the Hamlib/property review point. A narrow
compatibility adapter is acceptable if needed.

### Protocol and event flow

The current control path is:

    client command
      -> librrprotocol/srv.rigctl.c validation
      -> `rigctl` event
      -> rrserver/events.c
      -> backend.c policy wrapper
      -> selected backend
      -> backend-specific `cat.state` publication
      -> client VFO dictionary
      -> GTK, TUI, and CAT consumers

`ws_handle_rigctl_msg()` validates authentication, privileges, input, and wire
semantics. It emits a `rigctl` dictionary rather than invoking a backend.
`rrserver_handle_rigctlmsg()` consumes that event and calls the backend-neutral
setters. This event boundary must remain intact.

The `librustyaxe` event bus already supports dictionary-backed events through
`event_emit_dict()`. If other layers need property-change notifications, use
that event system and `event_on()` rather than introducing direct cross-layer
callbacks.

The existing `cat.state` wire format and command semantics must not change in
the first milestone.

### Room/VFO mappings

- `rrserver/database.c` persists room mappings as opaque binding strings such
  as `rig0.vfo_a` in the `room_vfos` table.
- `rrserver/events.c` adds, removes, and lists those strings but does not
  resolve them against a rig registry.
- `rrserver/main.c` initializes default `rig0.vfo_a` and `rig0.vfo_b`
  bindings.
- `rrclient/rooms.c` constructs equivalent `rig0.vfo_<letter>` strings.
- `librrprotocol/srv.chat.c` also represents room VFO availability as a mask.

There is no first-class rig identity registry yet. Room resolution is therefore
a downstream consumer of the future property model and is explicitly deferred.

### CAT consumers

`rrclient/cat*.c` implements local client-side radio emulation. It reads the
native client's VFO dictionary and sends websocket rig-control commands; it
does not own authoritative rig state. Some commands still contain VFO-A/B
assumptions. Do not restructure CAT until the property seam is reviewed and
stable.

## Proposed responsibility boundary

Do not treat these as prescribed structures or final API names. The required
behavioral boundary is:

1. A server-owned property service lives in `rrserver`, at or beside
   `backend.c`.
2. Backends submit typed observations and setter outcomes through it.
3. Every property set is owned by an explicit runtime rig object. The service
   owns last-known-value merging and change detection within that object.
4. A common publisher converts current server state into the existing
   `cat.state` wire representation and handles broadcast, rate limiting, and
   initial-state delivery.
5. Separate compatibility adapters project generic state into `vfos[]`,
   `rr_vfo_data_t`, and `cat.state`; the generic service must not depend on
   those representations.
6. Hamlib remains responsible for Hamlib API calls and radio-specific quirks.
7. Cross-layer notifications use the existing event bus.

The implementation must explicitly represent the difference between an
unavailable observation and a legitimate zero or false value. The current
poll merge uses zero and `MODE_NONE` as unavailable sentinels; blindly copying
that convention to arbitrary properties would make valid values ambiguous.

Descriptors/schema and runtime values are separate concepts. Even a minimal
Phase 1 descriptor should describe the property name, value type, and
read/write capabilities without embedding mutable current state. Runtime state
must distinguish unknown/never observed, known and available, and temporarily
unavailable. A failed read must retain a last-known value without reporting it
as currently available.

Standard property names must have one canonical definition rather than
scattered string literals. Initial names are dynamically generated VFO names
such as `vfo.A.frequency`, `vfo.A.mode`, and `vfo.A.width`, plus the limited
rig-level names needed by the milestone (`rx.vfo`, `tx.vfo`, `ptt`, and
`signal`). Do not add a comprehensive property catalog yet.

The common write entry point must carry a target rig, property, requested typed
value, and at least minimal source/context. It should perform central
validation, invoke the backend operation, and accept a later authoritative
observation. ACLs, room leases, and expanded audit policy remain deferred.

The following choices remain intentionally open until implementation begins:

- Property naming and whether names mirror wire keys or use domain names.
- Final public rig identity representation beyond the Phase 1 runtime object.
- Dictionary ownership and lifetime rules for snapshots and observations.
- Whether source, validity, or update-time metadata belong in dictionary
  metadata or a surrounding server-owned container.

## First implementation milestone

Before modifying production behavior, add regression coverage for:

- Existing Hamlib `cat.state` shape and values.
- Initial state sent to a newly authenticated client.
- Change suppression and forced/rate-limited state publication.
- Partial poll merging and last-known-value retention.
- FT-891 inactive-VFO behavior.
- Setter success/failure conventions and compatibility updates to `vfos[]`.
- Legitimate zero/false values remaining distinct from unavailable state.
- Two fake rig objects maintaining completely independent property state.

Then:

1. Verify and document `dict_value_t` string/pointer ownership, copy,
   destruction, lifetime, and enum/string conversion semantics.
2. Introduce a minimal runtime rig identity/object that owns an independent
   descriptor set and property state set using existing typed dictionary
   values.
3. Add a fake-rig test with VFO A at 14074000 Hz USB, VFO B at 7074000 Hz USB,
   and PTT false; prove two fake rigs do not share state.
4. Introduce neutral observation and change detection, including explicit
   unknown/known/unavailable state.
5. Route Hamlib frequency and mode observations through that facade.
6. Move Hamlib's state construction, comparison, rate limiting, broadcast, and
   initial-state delivery into a common temporary `cat.state` adapter above the
   property layer.
7. Route at least `vfo.A.frequency` writes through a common typed control
   request and the existing Hamlib backend operation. Expand to mode and other
   low-risk properties only as needed for a coherent milestone.
8. Preserve FT-891 behavior and temporarily preserve the old snapshots and
   wire behavior through adapters, without making them property dependencies.
9. Keep optimistic protocol updates separate from authoritative observed
   property state and document any remaining optimistic-update migration debt.
10. Keep non-Hamlib and non-GTK builds working.
11. Keep the internal backend working through its current implementation or a
   minimal compatibility path.
12. Run the component tests and affected build profiles.
13. Update this document with the chosen seam and any decisions made.
14. Stop and present the complete diff for review.

## Mandatory stop point

Stop after all of the following are true and the complete diff is ready for
review:

1. An explicit runtime rig object/identity exists.
2. Each rig owns an independent generic property set.
3. Two fake rigs coexist without sharing property state.
4. Hamlib frequency and mode observations enter the generic property service.
5. FT-891 last-known/unavailable behavior still works.
6. Generic property change detection is outside Hamlib.
7. `cat.state` is generated above Hamlib as a temporary adapter.
8. At least `vfo.A.frequency` writes traverse the generic control path and
   reach the existing Hamlib backend operation.
9. Existing FT-891 behavior still works.
10. Affected tests and builds pass.

Do not proceed in that implementation context to:

- Resolve room mappings against the new model.
- Make CAT commands consume the new model directly.
- Introduce multi-rig protocol semantics.
- Change WebUI behavior or wire messages.
- Remove legacy containers merely because the Hamlib path no longer needs
  them.
- Fully migrate the internal backend unless that is strictly necessary to keep
  it functional and the change remains a small compatibility adaptation.
- Enter media, sBitx, SDR, recording, ACL, or federation work.

## Risks to preserve during the milestone

- FT-891 reads have side effects and limitations: Hamlib may need to select a
  VFO before reading it, while mode or width reads for inactive VFOs can fail.
- PTT currently appears in connection state, global rig state, `vfos[]`, and
  backend state. Preserve it through compatibility code rather than forcing it
  into the generic model merely to complete a checklist.
- Power and signal-strength semantics are not yet a sound generalized
  property contract.
- Supported width lists are derived capabilities, not ordinary current-state
  values.
- Several backend functions use `false` for success and `true` for failure.
  Preserve existing conventions until a separately reviewed API cleanup.
- Optimistic updates emitted by `srv.rigctl.c` are not authoritative hardware
  observations and must not silently update canonical property state.
- Observable client behavior must stay synchronized with the WebUI where
  required by `doc/client-parity.md`. Add `PARITY:` comments when new mirrored
  behavior is introduced.

## Resume checklist

1. Read `AGENTS.md`, this document, `doc/client-parity.md`, and the current
   `CHANGELOG` entry.
2. Check the worktree and make the required safety commit before edits.
3. Reinspect `librustyaxe/dict.*`, `librrprotocol/vfo.*`,
   `rrserver/backend.*`, `rrserver/backend.hamlib.*`, and their current callers;
   the code may have changed since Phase 0.
4. Run relevant baseline tests and record failures before editing.
5. Locate existing Hamlib/backend tests and add component-local regression
   tests where coverage is missing.
6. Implement only through the Hamlib/property milestone above.
7. Update the changelog and this plan with the final design decisions.
8. Build/test affected configurations, show the complete diff, and stop for
   review before touching rooms or CAT consumers.

The Phase 1 report must list exact files and APIs changed, the ownership graph,
read and write flows, remaining compatibility adapters, remaining globals and
single-radio assumptions, anything that challenges the planned room-slot to
rig model, and the recommended next step.

## Phase 1 concrete design

Phase 1 introduced two deliberately separate server components:

- `rrserver/rig.properties.*` owns the generic runtime model.
- `rrserver/rig.compat.*` projects that model into the temporary `cat.state`
  wire representation.

`rr_server_rig_t` is opaque. It owns copied identity/name strings, a dictionary
of property entries, its backend association, its control handler, and its own
version sequence. The dictionary stores borrowed `VAL_PTR` references to
property entries; `rr_server_rig_free()` enumerates and destroys those entries
before freeing the dictionary. Two independently allocated rigs therefore
share neither state nor versioning.

Each property entry keeps descriptor/schema data separate from runtime state.
Descriptors currently contain name, `val_type_t`, readable/writable flags, and
an optional unit. Runtime state separately records the typed `dict_value_t`,
whether an observation has ever been attempted, whether a value is known,
whether it is currently available, a version, and the server's `now`
timestamp for the last state change.

### Typed-value ownership rules

- Descriptor names and units are copied and owned by the rig.
- Observation inputs are borrowed only for the duration of
  `rr_rig_property_observe()`.
- String observations are deep-copied into rig-owned storage.
- Numeric, boolean, and character values are copied by value.
- Snapshots borrow any returned string from the property entry; it remains
  valid only until that property changes or the rig is freed.
- `VAL_PTR` is rejected because the dictionary does not own pointed-to data
  and a generic property cannot infer a destructor.
- `VAL_NULL` is not a property value: known/available state represents absence
  without sacrificing legitimate zero, false, or empty-string values.
- Mode values are normalized strings such as `USB` in the generic layer.
  Conversion to/from `rr_mode_t` is confined to the current backend and
  compatibility boundaries.

### Introduced APIs

The public server-internal API now provides:

- Explicit rig construction, destruction, identity, and backend association.
- Descriptor definition and lookup.
- Dynamic `vfo.<ID>.<field>` name generation/parsing without fixed A/B struct
  members.
- Typed observations and explicit unavailable observations.
- Borrowed property snapshots with observed/known/available state.
- A typed control request containing target rig, property, value, source, and
  context.
- A synchronous per-rig control handler that validates descriptor existence,
  writability, and type before invoking the backend-facing handler.
- The `rig.property.changed` internal event, emitted only for canonical state
  or availability changes.

### Runtime ownership graph

    GlobalState (temporary single-server root)
      -> rr_server_rig_t "rig0"
           -> backend association
           -> independent descriptor/property dictionary
           -> backend control handler
      -> rr_cat_compat_t
           -> borrowed rr_server_rig_t
           -> per-VFO legacy publication cache/timers

`rr_backend_t.owner` is the present Hamlib-to-rig observation link. It remains
a single-owner field because the Hamlib backend itself is still a static
singleton; this is listed as migration debt rather than being hidden inside
the property service.

### Read/observation flow

    Hamlib read
      -> rr_rig_property_observe(rig, property, typed value)
         or rr_rig_property_unavailable(rig, property)
      -> per-rig comparison and state/availability update
      -> rig.property.changed event when canonical state changed
      -> legacy rr_vfo_data_t result returned to backend.c
      -> temporary vfos[] merge
      -> rr_cat_compat_t publication/diff/rate limiting
      -> existing cat.state clients

Hamlib now contains no `cat.state` construction, websocket broadcast,
last-message dictionary, or wire diff logic. Frequency, mode, and width are
submitted as generic observations. A failed inactive-VFO observation marks the
property unavailable while retaining any last-known value.

### Write/control flow

    existing rigctl request
      -> existing rrserver event handler
      -> rr_freq_set()/rr_set_mode() temporary input adapter
      -> rr_control_request_t (explicit rig/property/value/source)
      -> descriptor/type/write validation
      -> backend operation
      -> later Hamlib observation becomes authoritative state

A successful request still updates `vfos[]` as temporary client-compatibility
scaffolding. It does not update canonical observed property state. The existing
optimistic wire echo in `librrprotocol/srv.rigctl.c` also remains separate from
canonical state.

### Temporary compatibility adapters

- `rr_vfo_data_t` poll results and `vfos[]` merging remain in `backend.c`.
- `rr_cat_compat_t` generates existing Hamlib `cat.state` messages, preserves
  the comparison key set and unchanged-state interval, and sends initial state
  to newly authenticated clients.
- Existing `rigctl` events enter the generic frequency/mode control path via
  the old backend wrapper functions.
- The internal backend retains its old state and publication implementation.
- PTT, power, and supported-width capability semantics remain compatibility
  concerns and are not canonical generic properties yet.

### Migration debt after Phase 1

- `GlobalState` still exposes one `radio`, one selected backend, and one CAT
  compatibility adapter.
- `rr_backend_t` implementations and Hamlib's `RIG *`, caches, and reconnect
  state remain static singletons. Supporting multiple live Hamlib instances
  requires allocating backend instances per rig.
- `vfos[]`, `active_vfo`, and `rr_vfo_data_t` remain global compatibility
  state.
- The internal backend still duplicates old `cat.state` publication logic.
- PTT remains fragmented across connection, global, VFO, and backend state.
- Width writes have not entered the generic control path.
- Optimistic `srv.rigctl.c` updates remain migration debt.
- No generic client discovery/subscription protocol exists yet; the property
  event is internal.
- Property mutation assumes the server event-loop threading model and does not
  yet add per-rig locking.

The room-slot to rig design is still viable, but Phase 1 exposes its main
prerequisite: the fixed `rig0` allocation and static backend registry must
become a collection of runtime rig/backend instances before room binding
strings can resolve safely. Room work must not use `GlobalState.radio` as its
long-term registry.

## Recorded validation state

The Phase 0 baseline passed `selftest`, `rrserver`, and `librrprotocol`.

Phase 1 adds component-local tests for independent rigs, typed ownership,
unknown/known/unavailable state, legitimate zero/false values, string copying,
event change suppression, failed controls, initial `cat.state`, unchanged
publication suppression, frequency/mode changes, and last-known compatibility
output. The Hamlib-enabled `radio` profile builds and links against Hamlib.

## Phase 2 runtime multirig foundation

Phase 2 replaces the selected-backend singleton with a registry of runtime
rigs. It deliberately retains the existing single-radio configuration as an
implicit `rig0`; configuration parsing, room bindings, and client protocol are
not expanded in this phase.

### Ownership and destruction

    GlobalState
      -> rr_rig_registry_t
           -> entry (canonical UUID, config alias "rig0")
                -> rr_server_rig_t
                     -> property dictionary
                     -> rr_backend_t instance
                          -> immutable rr_backend_type_t
                          -> backend-private instance data
           -> entry (another UUID/alias)
                -> independent rig/backend/property state
      -> rr_cat_compat_t for the explicitly selected default rig only

The registry owns entries and rigs. Each rig references exactly one allocated
backend instance. A backend instance owns its copied configuration alias and
backend-private data. Registry destruction calls the backend type's destructor,
clears the rig association, frees the rig/property set, and finally frees the
entry. `GlobalState` frees the borrowing default-rig CAT adapter before freeing the
registry. Direct removal of the designated default rig is rejected until the
designation and borrowing adapter have been cleared.

`rr_backend_type_t` is immutable implementation metadata. `rr_backend_t` is an
allocated instance and contains the target rig, alias, active VFO, and private
data pointer. Every callback receives that instance. Two instances of the same
type therefore share code but no radio-specific runtime state.

Backend implementations register their immutable types with the generic type
registry. Build composition is isolated in `backend.register.c`; the runtime
rig registry contains no Hamlib/internal `#ifdef` or backend-specific branch.

### Registry API

`rrserver/rig.registry.h` provides construction/destruction, add/remove,
canonical UUID lookup, alias lookup, iteration, count, and explicit default-rig
selection. UUID and alias uniqueness are validated independently; neither
array position nor room slot is identity.

The backend instance API in `rrserver/backend.h` provides type lookup,
instance construction/destruction, instance alias/data accessors, explicit
per-rig polling, registry-wide polling, and explicit per-rig VFO capability
checks. The old `rr_freq_set()`/mode/PTT helpers remain default-rig adapters and
resolve only the registry's designated default rig.

### Stable UUID persistence

SQLite now owns a narrowly scoped identity table:

    rig_identities(
       identity_namespace TEXT,
       alias TEXT,
       uuid TEXT UNIQUE,
       created_at DATETIME,
       PRIMARY KEY(identity_namespace, alias)
    )

`db_rig_uuid_get_or_create()` returns the existing UUID for a namespace/alias
or uses GLib's UUID generator and persists a new one. Startup uses
`rig.identity-namespace`, falling back to `station.name`, together with the
implicit alias `rig0`. A build without SQLite can run but receives an ephemeral
UUID and logs that persistence is unavailable. Changing the namespace is an
identity change; operators needing identity independent of station renames
should configure `rig.identity-namespace` explicitly.

### Hamlib and internal backend instances

Hamlib's `RIG *`, native VFO cache, probe results, connection flag, reconnect
deadline/interval, model, copied device path, baud setting, and active VFO now
live in `hamlib_backend_t`, allocated once per backend instance. No mutable
per-radio Hamlib state remains file-global. Hamlib observations use
`backend->owner`, so one instance can update only its own property set.

The internal backend now similarly allocates its VFO state per instance. Its
duplicate `cat.state` construction and diff cache were removed; it submits
frequency/mode/width observations to the generic property service and uses the
same compatibility adapter as Hamlib. This was the minimum safe conversion
needed to prevent a second internal instance from broadcasting as the default
radio.

One scheduler iterates every registered rig and its supported VFOs. A failed
poll contributes to the aggregate failure result but does not stop iteration,
so disconnected rigs cannot starve later entries. Generic control requests
already carry a rig pointer and now dispatch through that rig's backend
instance; there is no selected-backend lookup beneath `rr_rig_control()`.

### Explicit default-rig compatibility boundary

The registry stores an explicit default rig pointer. Only this rig is
allowed to merge poll snapshots into global `vfos[]` or publish through the
single `rr_cat_compat_t`. Other rigs can poll and update generic properties but
cannot overwrite old client state. `rr_cat_state_send()` and every old
single-rig control wrapper target this designation.

Inactive-VFO first-poll seeding moved into the default-rig adapter. This preserves
the FT-891 fallback without making the fallback cache part of generic multirig
state. Existing global `vfos[]`, `active_vfo`, server PTT/TOT state, media
routing, old rigctl optimistic echoes, and room binding strings remain legacy
default-rig state by design.

### Configuration limitation and next migration

The runtime APIs can host multiple rigs and multiple instances of one backend
type, but startup currently instantiates only the old global backend settings
as alias `rig0`. Each Hamlib instance copies those values into private state;
there is not yet a parser for per-alias backend configuration. A future config
migration should enumerate named rig sections (for example `rig:rig0` and
`rig:rig1`) and pass an alias-scoped configuration view into instance creation.
The registry API must remain independent of that file format.

Rooms are still untouched. Existing `rig0.vfo_a` strings are not resolved by
the registry and aliases are not canonical room targets. Phase 3 should define
the generic rig/VFO discovery and object protocol before mapping room slots to
rig UUIDs.

### VFO object recommendation

Flat names such as `vfo.A.frequency` are sufficient for backend observation,
typed control, and arbitrary A-Z allocation today. They are not sufficient as
the final externally discoverable object model because a VFO will need its own
stable identity, capabilities, labels, availability, and possibly lifecycle
for dynamically allocated SDR receivers.

Before exposing the generic client protocol, introduce a lightweight
first-class VFO child owned by `rr_server_rig_t`. Keep the existing property
names as paths into that child so the Phase 1 property service does not need a
large rewrite. Do not use the VFO's letter or its array index as globally
canonical identity.

### Remaining migration debt after Phase 2

- PTT/TOT, `vfos[]`, `active_vfo`, media, and old client control remain scoped
  to the explicit default rig.
- Width, power, and PTT have not all moved through generic typed controls.
- The runtime configuration loader creates only implicit `rig0`.
- The Hamlib baud value is instance-owned but preserves prior behavior: the
  existing backend does not yet apply it to rigctld/network connections.
- Hamlib debug level is a Hamlib library-wide setting, not radio state.
- Property mutation still assumes the server event-loop threading model.
- The client-facing property discovery/subscription protocol does not exist.
- Room bindings still store unresolved legacy alias strings.
- Physical FT-891, reconnect, rrclient, and WSJT-X checks require the attached
  station and cannot be completed by the automated test environment.

Focused tests prove two UUID-addressed rigs using two instances of the same
fake backend type, independent private/property state, correctly routed
controls and observations, non-blocking polling failures, explicit default-rig-only
publication, registry removal isolation, alias lookup, and persistent UUID
reuse/differentiation.

## Phase 3 server object graph and named configuration

Phase 3 completes the internal server object graph without exposing it over a
new wire protocol:

    GlobalState
      -> rr_rig_registry_t
           -> rr_server_rig_t (canonical UUID, alias, display name)
                -> rig property store
                -> rr_backend_t instance and scoped config alias
                -> rr_server_vfo_t (canonical UUID, alias A, native ID A)
                     -> canonical VFO property store
                -> rr_server_vfo_t (canonical UUID, alias B, native ID B)
                     -> canonical VFO property store
      -> rr_cat_compat_t borrowing the configured default rig

A VFO belongs to exactly one rig. Its UUID is canonical; its display alias and
backend-native ID are separate fields. Traditional backends currently use a
single A-Z native ID adapter, leaving room for a display alias to change or for
a future backend to use a different stable native identifier. A VFO is the
common abstraction for physical VFOs and future SDR slices; there is no
separate slice object.

### VFO API and lifecycle

`rrserver/rig.vfo.h` provides add/remove, UUID lookup, local alias lookup,
iteration, count, identity/owner accessors, lifecycle access, and the
traditional native-index adapter. Removing a rig destroys all of its VFOs and
their property stores regardless of lifecycle.

`RR_VFO_PERSISTENT` denotes configured or backend-native VFOs whose identity
survives restart. `RR_VFO_EPHEMERAL` denotes runtime allocations, such as a
future SDR slice, that receive a UUID for their lifetime but are not written to
the identity database. Phase 3 does not allocate SDR VFOs.

SQLite persists configured VFO identity as:

    vfo_identities(
       rig_uuid TEXT,
       config_id TEXT,
       uuid TEXT UNIQUE,
       created_at DATETIME,
       PRIMARY KEY(rig_uuid, config_id)
    )

`config_id` is the stable configuration/backend-native identity, not
necessarily the display alias. `db_vfo_uuid_get_or_create()` reuses the UUID
for a rig UUID plus config ID after restart. Different rigs can both own an
alias/native ID `A` without collision because their canonical rig UUID scopes
the mapping. Ephemeral VFO creation does not call this helper.

### Canonical property ownership and compatibility paths

VFO properties now have one canonical value in the VFO object's property
store:

    target UUID: <VFO UUID>
    property: frequency | mode | width

The old rig path `vfo.A.frequency` is a resolver: it finds local alias `A` and
delegates to the same VFO property entry. It is not a second property or a
copied value. Canonical and compatibility reads therefore return the same
value, availability, and version. Property events identify the owning rig and
canonical VFO target while retaining a compatibility path where applicable.

Control normalization follows the same rule. A canonical request carries the
VFO object and local property name. A legacy request carrying a rig plus
`vfo.A.frequency` is resolved once by the property core, and the backend
handler receives the owning rig, VFO object, and local `frequency` name.
Backends never search a process-global current VFO.

Rig-level properties remain in the rig property store. Future `rx.vfo` and
`tx.vfo` values can therefore hold VFO UUIDs internally while the CAT adapter
continues translating A/B for old clients. That selection migration is not
performed in Phase 3.

### Exact named-rig configuration

The old `backend.active`, global Hamlib settings, and global `rig.vfos` syntax
is replaced by a deterministic alias list plus scoped sections:

    [general]
    rig.instances=rig0 rig1
    rig.default=rig0
    rig.identity-namespace=my-stable-node-name

    [rig:rig0]
    name=FT-891
    backend=hamlib
    vfos=A B
    state-interval=15
    hamlib.model=2
    hamlib.device=127.0.0.1:4532
    hamlib.baud=38400
    reconnect-interval=30

    [rig:rig1]
    name=Second radio
    backend=hamlib
    vfos=A B
    state-interval=15
    hamlib.model=2
    hamlib.device=127.0.0.1:4533
    hamlib.baud=38400
    reconnect-interval=30

Aliases use letters, digits, underscore, or hyphen. `rig.instances` order is
configuration order only and is never canonical identity. Duplicate aliases,
unknown backend types, unsupported/duplicate VFO aliases, missing VFO lists,
and invalid `rig.default` references are startup errors. With one configured
rig, an omitted `rig.default` selects that rig. Multiple rigs require an
explicit default, preventing hash/list order from choosing compatibility
behavior.

The config parser stores section values as `rig:<alias>.<key>`, but backend
implementations do not assemble those keys. Each backend receives its instance
alias and uses `rr_backend_config_get*()` as its scoped view. Thus two Hamlib
instances can independently configure model, device, baud, reconnect timing,
and future backend options.

### Startup and destruction

Startup registers backend types, opens the persistent store, creates the rig
registry, parses `rig.instances`, and then performs for each alias:

1. Resolve the backend type and scoped values.
2. Reuse/create the persistent rig UUID.
3. Allocate the rig and its backend instance.
4. Reuse/create each persistent VFO UUID.
5. Add the VFO child and define its canonical properties.
6. After all rigs succeed, resolve and install `rig.default` and its CAT
   compatibility adapter.

Any invalid rig or constructor failure aborts startup and destroys every rig,
backend, VFO, property store, alias, and partially created registry entry.
Normal shutdown frees the borrowing default CAT adapter first, then each
backend, its VFO/property children, its rig, and finally the registry.

### Compatibility boundary and remaining debt after Phase 3

Only the configured default rig feeds `vfos[]`, `active_vfo`, `cat.state`, old
rigctl controls, PTT/TOT, and legacy media channels. Inactive-VFO fallback and
current FT-891 CAT semantics remain in that adapter. Hamlib's debug setting is
still library-global, and Hamlib baud remains instance-owned but is not applied
to NET rigctl connections.

Room records remain intentionally unmigrated. Existing bindings such as
`rig0.vfo_a` and the authoritative room's historical `-rig0` name contain
aliases rather than canonical rig/VFO UUIDs. In particular, choosing a default
alias other than `rig0` does not make those old binding strings canonical.
Phase 4 must not reinterpret them by registry order; it should introduce room
slot records that directly reference rig UUID and VFO UUID.

Other remaining work includes generic width/power/PTT controls, UUID-valued
RX/TX VFO selection, client discovery/subscription, dynamic VFO allocation,
threading beyond the current event loop, and physical FT-891/reconnect/client
validation on an attached station.

### Discovery/property protocol recommendation

The next protocol should expose typed objects rather than paths derived from
aliases. Discovery should return rigs by UUID with alias, name, backend type,
capabilities, and child VFO UUIDs; VFO records should include owning rig UUID,
alias, native/display metadata, lifecycle, capabilities, and property
descriptors. Snapshots and change events should carry target UUID, property
name, type, availability, value, and version. Controls should use the same
target UUID/property pair. Alias paths should appear only as explicit legacy
compatibility fields.

Discovery and property subscription should be completed before room slots are
mapped, so room configuration can store canonical rig/VFO UUID references
without defining a second identity scheme. No discovery messages, room
mappings, CAT migration, or media redesign are part of Phase 3.

## Phase 3 live validation (2026-10-03)

The source configuration enumerates `rig0` (internal, default) and `rig1`
(Hamlib NET rigctl). A diagnostic executable links the production registry,
configuration parser, persistence, internal/Hamlib backends, and CAT adapter.
It dumps the actual registry and canonical VFO snapshots from that process;
only outbound client sends and the application shutdown handler are captured
for assertions. It does not depend on client discovery or start media.

Run the repeatable integration test with its own loopback dummy rigctld:

```sh
bash rrserver/tests/test_multirig_live.sh
```

The normal rrserver suite discovers this wrapper. Hamlib, SQLite, Python 3,
and rigctld are required; loopback sockets must be allowed. It tests online
polling, disconnect, reconnect, retries disabled, initially unavailable
endpoint, default-only CAT/vfos[] output, and process/database reopen with
all six rig/VFO UUIDs unchanged and matched to database rows. Disconnect
tests terminate only the dummy daemon created by the test.

To inspect the source configuration against its configured Hamlib endpoint:

```sh
bash rrserver/tests/test_multirig_live.sh --configured-endpoint
```

This mode uses `config/rrserver.cfg` and a disposable database. It prints rig
aliases, UUIDs, backend types, VFO UUIDs, scoped connection settings, and
canonical frequency/mode availability. It never kills the existing daemon
or sends frequency/mode/PTT controls to Hamlib, but normal Hamlib polling
selects VFOs. The test changes only its private internal backend frequencies
to prove continued polling and compatibility isolation. Printed UUIDs belong
to the diagnostic database, not the installed server's database. This mode
reports unavailable properties without claiming a successful observation.

On this host, the existing endpoint was `rigctld -m 1 -o` (dummy hardware),
not an FT-891. The configured-endpoint diagnostic connected with model 2,
device `127.0.0.1:4532`, baud 38400 (default; not a NET transport setting),
and reconnect interval 30. Canonical rig1 A/B observations were respectively
145000000/146000000 Hz and FM; rig0 remained independently USB. Direct rig1
polling emitted no legacy CAT state and left `vfos[]` unchanged. Registry-wide
polling updated both real backend implementations.

Observed diagnostic identities (stable across both process runs):

| Object | UUID |
| --- | --- |
| rig0 / internal / default | d4a52d7d-44da-4619-87c9-7beeb999b4f7 |
| rig0 / A | d378ea97-a639-46bf-8cd1-afe9530b6afd |
| rig0 / B | 3ed8d1e5-01bd-4fa0-ab87-0b9a918d432b |
| rig1 / Hamlib | cbd8f058-0344-477d-8da2-b94122fdac60 |
| rig1 / A | 0c5ba483-6815-4d25-b2cd-44df71621cce |
| rig1 / B | 73fdee69-7338-4b1a-8c0b-b463cc80af00 |

Two genuine disconnect bugs were fixed:

- Losing the Hamlib connection during VFO selection left canonical properties
  marked available. All that instance's VFO frequency/mode/width properties
  now become unavailable, retaining last-known values until new observations.
- A non-default Hamlib instance with reconnect interval zero requested whole
  server shutdown on connection failure. It now remains offline. The default
  rig retains its existing supervisor restart policy.

Startup logs now include VFO identities and the effective Hamlib model,
endpoint, baud, and reconnect interval. Existing logs already identify each
runtime rig/backend and the selected default. There is no added per-poll log.
With access to the running server's log, inspect these records using:

```sh
grep -E 'Runtime rig|Default rig|VFO [A-Z] \(|connecting to|connected to|connection lost|retrying|remains offline' /var/log/rustyrig/rrserver.log
```

The installed server was running as `rustyrig`; its log/database could not be
read with the available permissions (`sudo -n` requires a password). Thus its
specific in-memory registry and loaded configuration were not inspected.
The installed `/etc/rustyrig/rrserver.cfg` still contains old `backend.active`
syntax, unlike the source configuration; do not assume which one that process
loaded. Successful live results above are from the diagnostic process using
production backend code and the existing endpoint. Server timer inspection
confirms it calls `rr_backend_poll_all()`.

Isolation here means independent state and continued polling, not bounded
latency: Hamlib I/O is still synchronous and transport timeouts can delay the
shared event loop. Physical FT-891 and installed-process validation remain
separate from the successful dummy-rigctld test. Phase 4 remains unstarted.

Validation after the fixes: `make -j2` (Hamlib enabled) and
`./tests/run-tests.sh selftest rrserver librrprotocol rrclient` passed. The live
integration test also passed under ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0`, `CFLAGS='-fsanitize=address,undefined
-fno-omit-frame-pointer -g'`, and `LDFLAGS='-fsanitize=address,undefined'`.
cppcheck warning/performance/portability checks on the touched C code passed
with the project feature defines and `MG_ARCH=1`; `git diff --check` and
shell syntax validation passed. Suite and sanitizer output from this run is
saved in `/tmp/rustyrig-phase3-live-tests.log` and
`/tmp/rustyrig-phase3-live-sanitizers.log` respectively.
