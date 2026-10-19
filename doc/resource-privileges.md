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
