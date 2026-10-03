# Rig property migration plan

Status: Phase 1 implemented through the Hamlib/property stop point; awaiting
review before any room, CAT-client, or later migration work.

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
