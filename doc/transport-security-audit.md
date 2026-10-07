# HTTP, WebSocket, serial and CAT audit — 2026-10-06

Scope: RustyRig's own transport wrappers, JSON parser, command dispatch,
serial exports/local CAT endpoints and PTT ownership. The deployment remains
LAN/VPN-only. Password-protocol migration and broader abuse protection are
explicitly deferred by the operator. No production database, configuration,
credentials or services were changed.

## Findings fixed

| Area | Finding | Result |
| --- | --- | --- |
| JSON input | A trailing backslash could advance the string scanner beyond NUL. Nesting was unbounded. | Safe token scanning and a 64-level nesting limit; malformed messages are discarded. |
| JSON semantics | Missing commas/trailing commas, invalid or overflowing numbers, duplicate fields and malformed escapes could be accepted or interpreted inconsistently. Escapes were not decoded correctly. | Strict separators/numbers, duplicate-field rejection, correct escape/UTF-16 pair decoding; embedded NUL/lone surrogates rejected because the dictionary uses C strings. |
| Numeric conversion | Floating values at the rounded signed/unsigned integer upper boundary could pass a getter's range check and produce an undefined conversion. | Use exclusive, exactly representable upper bounds before casting. |
| WebSocket | Embedded NUL could hide trailing bytes; opcode testing used a bit check rather than the opcode value; non-object roots were parsed despite the command format requiring an object. | Exact text/binary opcodes, NUL rejection, JSON-object roots and the existing 65,535-byte text limit. |
| Binary framing | A declared payload shorter than the received message left unvalidated trailing bytes. | Require exactly one complete binframe in C and the browser. |
| HTTP routing | Prefix matching allowed `/ws-extra` and API lookalikes to invoke real routes. | Match exact routes, optionally with one trailing slash. Accept GET/HEAD only. |
| HTTP files | Raw-path checking missed encoded unsafe components and hidden files. Symlinks could expose files outside the configured web root. | Decode/validate paths, reject hidden/control/backslash/NUL components, check POSIX realpath containment including index/gzip alternatives. Retain safe internal symlinks and normal MIME/404 handling. |
| HTTP diagnostics | `/api/stats` exposed connection addresses to unauthenticated HTTP clients. | Apply account admin/owner checks. Normal HTTP has no authenticated account context, so the endpoint returns 403 until an HTTP authentication mechanism exists. |
| Serial lifecycle | Acknowledged stream IDs could be reused, allowing an old sequence-1 packet to write to a newly opened device. Stream 255 was unavailable. | Issue all 255 nonzero IDs at most once per connection; reconnect after exhaustion. Failed opens do not consume an ID. |
| Serial closure | The pending-close flag survived a reopen and could close a later attachment unexpectedly. | Reset pending-close state with the other connection state. |
| Serial input | Numeric control fields could be coerced/truncated; a running tunnel's password-expiry/change policy was not checked while polling. | Validate stream/sequence/baud bounds and current account policy before device I/O. |
| Account administration | Target protection and staff creation passed `admin|owner` to a single-flag matcher, bypassing intended owner-only checks. | Evaluate account staff flags explicitly; admins cannot modify owners/other admins or create staff accounts. Live tests cover lock/unlock/remove/reset/pass/privileges and ordinary-user denial. |
| CAT targets | Unmapped or malformed VFO names were processed before PTT ownership changes or state broadcasts. | Require a single canonical VFO mapped to the selected room before any control effect. |
| CAT/PTT ownership | A TX-capable session could send key-up while another session held PTT. A keyed session could switch transmitting rig/VFO without releasing. Duplicate key-down restarted hardware/TX-timeout processing. | Owners may stop admins and lower roles; admins may stop lower roles but never owners; TX/elmer may stop noobs. Equal roles cannot stop one another. Overrides release the actual holder without transferring ownership or keying the requester. Duplicate own key-down is acknowledged without reapplying it. |
| CAT values | Malformed mode/width values were broadcast as state; frequency values could exceed the signed 32-bit CAT/backend range. Local Yaesu `atol` accepted malformed numbers. | Validate before broadcasting/applying. Preserve named widths and numeric `Hz` labels, frequency units and command chains; reject junk, nonfinite and overflowing values. |
| PTT safety | Station TX lockout also blocked key-up. | Lockout inhibits key-down only; safety release still reaches the backend. |
| PTT rejection | Station/backend rejection could leave a session claiming PTT; failed key-up also cleared the TX timeout. | Report failure, correct session/CAT state, preserve release ownership and the timeout when key-up fails. |
| Native TLS setup | TLS was initialized after the WebSocket upgrade rather than at TCP connection. | Start TLS before upgrade; initialize server-side option structures. |
| Named delivery helper | The legacy, currently uncalled `ws_send_to_name` helper broadcast to unrelated sessions and rejected server-originated sends. | Send only to authenticated WebSocket sessions matching the requested account. |

## Authorization checks retained

Serial discovery/open/configure/read/write/close use current account privilege
flags and the configured per-export access expression. Physical paths are
server configuration only; client requests cannot select arbitrary paths.
Exports have exclusive session ownership, bounded queues, exact binary framing,
sequence checks and disconnect/revocation cleanup. Server GPS ports are not raw
client-writable exports. Local CAT uses the same server room/privilege/PTT gates
as native widgets and browser controls. UUID property SET uses typed schema,
account privileges, room/rig ownership and independent-RX tuning checks.

Read-only listening still needs no RX privilege. Account flags control PTT,
controls and explicit codec switching. Authentication wire messages remain
unchanged; invalid representations are rejected more consistently.

## Remaining findings and limits

- Native TLS still defaults to `ca="*"`, disabling peer certificate verification.
  Correct TLS startup does not establish server identity. A configurable private
  CA/pinning policy is a follow-up; this pass preserves existing LAN/VPN trust
  settings. Browser HTTPS uses browser certificate verification.
- Legacy CAT notifications still precede backend application; UUID property
  observations supply confirmed frequency/mode/width state. PTT now corrects
  rejected requests. A failing hardware/backend key-up cannot guarantee that
  RF physically stops; field validation of hardware protection and release
  failures remains necessary.
- Serial access grants arbitrary bytes to its allowlisted physical device.
  A raw serial export wired to a rig can therefore bypass that rig's CAT/PTT
  policy by design. Access now requires explicit account flags `serial` or a
  matching `serial.<portname>` (including `serial.ttyGPS*`); admin/owner alone
  grants no access. Grant these flags only to accounts trusted with the entire
  selected device. Revocation closes active tunnels.
- POSIX static-file containment assumes the locally configured web root is not
  concurrently modified by an untrusted local process. Race-proof descriptor
  traversal would require a filesystem-serving change; non-POSIX builds retain
  lexical checks and their existing filesystem abstraction.
- Mongoose receive buffering has its existing 3 MiB cap; complete application
  text/binframes retain the smaller protocol limits. Connection/send-queue budgets,
  distributed abuse and the dependency's entire HTTP/WebSocket/TLS parser are
  not exhaustively assessed or changed. Nothing in `ext/` was modified.

## Validation

Existing librustyaxe, librrprotocol and rrserver suites passed before changes.
After changes, shared-library, protocol, server, native-client and browser
regression suites passed. Added checks cover truncated JSON prefixes, malformed
JSON/Unicode/numbers, nesting, duplicate fields, numeric cast boundaries,
WebSocket opcode/root/NUL/size, exact binary lengths, HTTP route/method/path/
symlink behavior, raw CAT/PTT ownership and values, all 255 serial stream IDs,
stale packets after acknowledged close/reopen and TX-lockout key-up. JSON and
numeric regression inputs also run with address/undefined-behavior/leak sanitizers.

The live fixtures use disposable databases, internal rigs and local PTYs.
They do not key physical hardware or modify deployed accounts. GTK and non-GTK
builds are checked separately. Browser binframe validation mirrors C.
