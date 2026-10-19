# v1 resource privileges — implementation contract

The v1 database separates durable resource grants from the system roles stored in
`users.permissions`. This is a staged change: **the resource_privileges table is
not yet consulted by authorization checks**, so an administrator must not rely on
it for enforcement until the resolver and all call sites have migrated.

## Subject identity

Input syntax is `user@host` (for example `alice@camp`). Here `host` is an
advertised station/site name, not a user-provided UUID or a network hostname.
An unqualified `alice` means a user on the local server. Resolve the host through
the authenticated link tree; resolve the user through that server's authoritative
user identity information. Do not trust user IDs or privilege claims in a client
request. Unknown/offline hosts, deleted users, name collisions or missing proofs
must fail closed.

The grant table stores subject_user and subject_host independently. The
`resource_uuid` column identifies the protected resource, never a transient
connection. A subject can be re-resolved after site reconnect or user rename;
renames need an explicit policy because `user@host` refers to names. Persist
the resolved identity for a request only as a cache with invalidation on identity
or topology changes.

## Grants

`resource_privileges` contains `resource_kind`, `resource_uuid`, `privilege`,
`effect` (allow/deny), issuer, and optional expiry. Grant resolution must be
server-side and consider the authenticated subject, exact resource, and expiry.
Default is deny; an explicit deny must take precedence. Owner/admin's authority
to administer grants does **not** imply bypassing a hardware safety policy.

Core roles and authentication flags stay on the user record; permissions tied
to a rig, VFO, room, media stream, serial port, file share, or bridge belong in
the new table. Existing `has_priv(uid, ...) ` checks must not infer resource
permission from an unrelated global permission string after migration.

## Reusable privilege sets

The v1 schema includes `privilege_sets`, `privilege_set_entries` and
`privilege_set_assignments` so a reusable policy such as `hf-listen` can
hold `rx`, `spectrum.read` and `frequency.read`. Assign it to
`alice@camp` for one rig UUID, and to `bob` for another, without copying
individual grants. A set is a flat collection of operation names, **not**
an inheritable system role. No recursive set inclusion means evaluation is
bounded and predictable.

```sql
INSERT INTO privilege_sets(name, description, created_by)
VALUES('hf-listen', 'Receive-only HF access', 'admin');

INSERT INTO privilege_set_entries(set_id,privilege)
SELECT id, 'rx' FROM privilege_sets WHERE name='hf-listen';

INSERT INTO privilege_set_assignments
   (set_id,subject_user,subject_host,resource_kind,resource_uuid,assigned_by)
SELECT id,'alice','camp','rig','REPLACE-WITH-AUTHORITATIVE-RIG-UUID','admin'
FROM privilege_sets WHERE name='hf-listen';
```

The new `db_resource_access()` evaluator reads both direct and set-derived
grants. Direct or set-derived `deny` wins over every `allow`. Expired
assignments/grants do not apply, missing grants return `RR_RESOURCE_UNSET`,
and database errors fail closed. The call must receive a server-authenticated
subject, never raw subject fields supplied by a client.

**Partially enforced:** ordinary linked-site room join/part/chat now defaults to
allowed when no resource grant matches; explicit deny still blocks access.
Other relayed actions continue requiring explicit `rig.use` or `room.join`
grants, and configured bridge room/media
admission checks can use `bridge.use` from a rig grant. Direct grants and
set assignments are both evaluated, with explicit deny taking precedence.
Local account system privileges remain as a fallback for bridge admission
when no resource grant matches. Unconverted features (serial, shares,
some VFO/TX/property paths) are **not yet resource-grant aware**; a new grant
does not automatically authorize every subsystem.

### Host-wide rig access preset

A reusable `rig.full` privilege-set entry grants every rig-scoped operation
on a matching rig, subject to independent room, radio hardware, and PTT safety
checks. Assign the set to `rig *` to cover **every rig owned by this server**,
including rigs added later. More specific `deny` grants still win.

On the **rig-owning server**, create the preset once:

```text
/admin privset-create full-rig Full rig access
/admin privset-entry full-rig allow rig.full
```

Then assign it to an authenticated local user or linked-site principal:

```text
/admin privset-assign full-rig alice@camp rig *
```

Remove access with:

```text
/admin privset-unassign full-rig alice@camp rig *
```

This is scoped to the owning server; it is **not** a network-wide admin role.
The remote identity is taken from the authenticated relay session, not from
client-supplied role assertions.

### Administration commands

Run these on the **owning server** while authenticated as `owner`:

```text
/admin privset-create hf-listen Receive-only operations
/admin privset-entry hf-listen allow rig.use
/admin privset-entry hf-listen allow bridge.use
/admin privset-assign hf-listen alice@camp rig 12345678-1234-4234-8234-1234567890ab
/admin privsets
/admin setentries
/admin assignments
/admin resource-grant alice@camp rig 12345678-1234-4234-8234-1234567890ab rig.use allow
/admin grants
/admin resource-revoke alice@camp rig 12345678-1234-4234-8234-1234567890ab rig.use
/admin privset-unassign hf-listen alice@camp rig 12345678-1234-4234-8234-1234567890ab
/admin privset-unentry hf-listen bridge.use
/admin privset-delete hf-listen
```

Use the actual persistent UUID from Resource Explorer or the Rooms admin tab.
An unqualified username (e.g. `alice`) identifies a **local** subject; a
qualified name (e.g. `alice@camp`) identifies a remote site principal.
Resource permissions do not provision a local user or a login credential.

The originating link is authenticated and checked for a valid node route,
but **cross-site authoritative user-UUID lookup is not yet implemented**.
Site names are resolved from authenticated relay/catalog metadata, so the
linked site is currently trusted to assert the originating username.
Do not connect untrusted peer servers until that protocol step is complete.

## Migration work still required

1. Implement a server-owned resolver that validates `user@host` using the
   authenticated link tree and the remote site's authoritative subject identity.
   Design an authenticated identity lookup message; do not bind grants to
   untrusted numeric UIDs supplied by clients.
2. Implement transactional grant/revoke/list operations, enforcement, auditing,
   denial diagnostics, and expiry handling.
3. Introduce resource-aware authorization calls and convert every rig, VFO,
   room, media, serial, share, bridge, and control operation. Add negative
   tests for spoofed site names and unknown links.
4. Update rrclient GTK/TUI and browser admin privilege editors to show
   system roles and per-resource grants separately.
5. Remove resource tokens from `users.permissions` after the enforcement
   migration is verified. No pre-1.0 wire or database compatibility is required.

## Generic exported node controls

The persistent **server node** now owns a generic typed control namespace.
This reuses `object.snapshot`, `property.descriptor`, `property.state`,
`property.changed`, and `property.set` rather than inventing an LCD-specific
wire message. Resource Explorer can discover the node's controls.

`rrserver/platform.controls.h` registers trusted local providers with a
stable name, display label, unit, numeric bounds, step, read/write functions,
and declared privilege. The current implementation handles **integer controls**;
it is deliberately bounded (maximum 64 controls). Invalid names, unavailable
devices, out-of-range or off-step values, and writes to read-only controls
are rejected server-side. No remote request can supply a sysfs path or callback.

The first provider is `display.brightness`, backed by Linux's
`/sys/class/backlight` when exactly one device exists, or a configured
local device. The client Admin → Controls tab discovers providers and uses a
generic selector and slider. Equivalent admin commands are:

```text
/admin controls
/admin control display.brightness
/admin control display.brightness 90
```

Controls also appear as properties on the server node UUID. Owner/admin
authorization is currently required for writes; advertised per-control
privilege metadata is reserved for the unified resource privilege enforcement
pass. Writes are audited, and both the property API and admin UI publish
changes to subscribers. Ordinary operators cannot write the backlight.

```ini
[general]
platform.backlight.device=
platform.backlight.minimum=1
```

The device setting accepts a basename only. Minimum brightness defaults to
1 to prevent remote operators blacking out a display. The process still
requires OS write access to the backlight sysfs file.

### Other candidates evaluated

- **Speaker output volume / mute:** strong candidates for another *node*
  control on sBitx; the driver should read and write the actual ALSA or
  hardware mixer state. The existing `audio.volume.rx` is a *client-local*
  fwdsp volume preference and must not be confused with server hardware
  speaker volume.
- **Fan speed, display power, battery/generator switches:** potentially node
  controls, but only with appropriate hardware-specific bounds and safety
  interlocks. Remote power-off actions must not be exported as ordinary
  unrestricted sliders.
- **GPIO exports:** reuse the mechanism for deliberately configured
  non-critical outputs. Do not export arbitrary GPIO devices or pins; the
  existing GPIO mappings and rig-owned outputs must preserve ownership.
- **Si5351, VFO frequency/mode, amplifier/tuner settings:** these remain
  **rig/VFO properties** with their existing hardware/control semantics,
  rather than moving to the node simply because they are adjustable.
- **Media sinks and per-user audio routing:** remain session/room scoped.
  They should eventually use typed descriptors on their own resource
  identities rather than a global node volume knob.

Next up for the authorization project: map a node-control write to a
`site`-scoped `resource_privileges` grant keyed by the persistent node UUID,
while retaining the system `owner|admin` override and explicit deny policy.
This is **not yet implemented**.
