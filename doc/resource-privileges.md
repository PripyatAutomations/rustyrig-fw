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

**Partially enforced:** the owner-side relay now requires explicit `rig.use`
or `room.join` grants for remote requests, and configured bridge room/media
admission checks can use `bridge.use` from a rig grant. Direct grants and
set assignments are both evaluated, with explicit deny taking precedence.
Local account system privileges remain as a fallback for bridge admission
when no resource grant matches. Unconverted features (serial, shares,
some VFO/TX/property paths) are **not yet resource-grant aware**; a new grant
does not automatically authorize every subsystem.

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

## sBitx / Pi LCD control

Expose a backlight capability only when the server detects an administrator-
configured and validated Linux backlight device under
`/sys/class/backlight/`. Do not assume every Pi or sBitx uses the same
controller or supports writes. The server should expose current and maximum
brightness and an owner/admin-only set operation with a bounded numeric value.
Do not accept arbitrary filesystem paths, shell fragments, or sudo commands
from rrclient; the path must be configured locally and verified at startup.
A minimum usable brightness should be configurable to avoid leaving a
headless remote operator with a completely dark display. Audit all writes and
return clear errors for missing driver, read-only sysfs, or unavailable display.

The UI can render a slider if the capability is advertised, otherwise omit
the control. These are *planned* runtime features, not shipped by the schema
change.
