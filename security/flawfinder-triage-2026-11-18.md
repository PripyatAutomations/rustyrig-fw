# Flawfinder triage — 2026-11-18

Input: Flawfinder 2.0.19, `-m 2`, 187,444 lines analyzed.
This is a review queue, not a count of demonstrated vulnerabilities.

| Risk level | Raw scanner hits |
| --- | ---: |
| 5 | 7 |
| 4 | 159 |
| 3 | 48 |
| 2 | 1,927 |

Of the 166 level-4/5 hits, 55 belong to test fixtures and 111 to other
sources. They represent 154 distinct file/line/rule combinations because
Flawfinder expands some findings multiple times at the same source line.

## Reviewed

- `rrserver/relay.catalog.c:366`: directory node copied into a 37-byte
  field. `rr_relay_directory_valid()` previously did not check the node
  identifier when the directory had no client keys. Fixed by validating a
  UUID before storing; `rrserver/tests/relay_users.c` checks malformed
  and oversized IDs.
- `rrserver/link.tree.c`, `link.mesh.c`: fixed-size UUID copies follow
  explicit UUID validation; site names are bounded. Keep their validation
  tests instead of replacing these operations for warning-count reduction.
- `rrserver/relay.sessions.c`: remote session UUID copies follow
  `rr_object_uuid_valid()`. Remaining relay string handling deserves a
  separate protocol-focused review.
- `rrserver/bridge.hints.c`: modes are length-checked before copying.
- `rrclient/cmd.completion.c`: suffix allocation includes the terminator.
- `rrserver/files.c:763-771` and `relay.catalog.c:2342`: many
  `access` race warnings are lexical matches on the `access` variable,
  not `access()` filesystem calls. These are not TOCTOU findings.
- `rrserver/performance.c:95`, `relay.catalog.c:1067`: the
  `readlink()` results are NUL-terminated; they only inspect
  `/proc/self/fd` for telemetry.
- `rrclient/serial.c:263`: `readlink()` is terminated correctly, but
  comparing a symlink target before `unlink()` remains subject to a
  local filesystem race if an attacker can modify the containing directory.

## Next review

Prioritize actual file-opening/permission checks in `rrserver/srv.chat.c`,
`rrserver/relay.catalog.c` and any share or serial paths whose parent
directories could be modified by an untrusted local account. Trace the
format warnings to their call sites; variadic wrappers do not imply
attacker-controlled format strings. Keep C23, optional build profiles,
and the separate librrprotocol trust boundary in scope.

## Baseline interpretation

The audit comparator now gives separate identities to repeated Flawfinder
call sites. Its version-2 finding IDs include line numbers for this scanner.
That can create new/resolved pairs when code moves; review source diffs
before calling those fixed. Old version-1 state starts a new reference
instead of reporting fictitious closures.
