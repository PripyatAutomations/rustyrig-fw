# Hamlib worker migration — IPC foundation

This stage adds an **inactive** versioned message format for a future per-rig
`rrhamlib` process. The production `backend.hamlib` module remains unchanged;
**no current PTT operation uses this protocol**.

The transport is a private `socketpair(AF_UNIX, SOCK_SEQPACKET, ...)` created
before spawning one worker per physical rig. There is no public listener. Each
packet starts with a 20-byte network-order header (magic, version, type,
sequence, payload length, reserved flags), then at most 4096 bytes. Receivers
must reject truncated, oversized, unknown-version, unknown-type, and
nonzero-flags messages. Both peers must enforce bounded command queues and
deadlines; the framing limit alone does not prevent blocked Hamlib calls.

## TX safety gates before enablement

- Server authenticates and authorizes TX, enforces existing interlocks and
  accounting, and gives RELEASE priority over ordinary work.
- Worker exclusively owns the physical CAT port. An independently expiring
  *monotonic-clock* lease must be enforced inside the process driving PTT.
- Worker startup/restart must establish a verified OFF state before accepting
  KEY_ON. A failed PTT release is **unknown/unsafe**, never success.
- Worker must respond to missed renewals, parent loss, closed IPC, and normal
  shutdown by attempting an OFF operation; rekeying after reconnect is forbidden.
- Never let an indefinitely blocked CAT call prevent lease expiry from being
  enforced. Design the Hamlib I/O deadlines and watchdog accordingly.
- `SIGKILL`, host power loss, a hung kernel driver, or a radio retaining PTT
  cannot be fixed by process-local cleanup. An independent hardware TX-enable
  watchdog is required to make a stronger RF-off guarantee.
- Do not automatically enable the subprocess backend until fault injection
  covers worker/parent death, blocked CAT, renewal loss, and reconnect during TX.

Protocol unit test (no hardware): `make test-hamlib-ipc`.
