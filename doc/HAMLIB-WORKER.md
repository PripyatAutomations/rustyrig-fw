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


## Backend naming and packaging migration

Target topology:

- `backend=hamlib` selects the **new external-process IPC proxy**. It must
  never load or link against `libhamlib` in the server process.
- `backend=hamlib-legacy` explicitly selects the old in-process module.
  The compatibility module keeps the filename `rrserver-hamlib.so` for package
  continuity but registers **only** the type `hamlib-legacy`.
- `rrhamlib` belongs in `/usr/libexec/rustyrig/rrhamlib`, alongside other
  private subprocesses (modems are under its `modems/` subdirectory).
- The existing `rr_backend_t` API, typed properties and server-owned worker
  queue must remain stable. The proxy transforms operations to IPC; the child
  exclusively owns the physical Hamlib device.
- Never open the same radio through both backends at once.

**Migration status:** The server now registers a subprocess-backed `hamlib`
type when Hamlib support is selected at build time; the original module
registers only `hamlib-legacy`. The IPC proxy implements strict request/response
for polling, frequency, mode, numeric width, split, VFO swap, and verified
PTT release. The worker rejects TX_ON and refuses CAT mutations if PTT is on
or unknown; it confirms targeted VFO selection before mutations.

**This is not feature-parity complete or ready for deployment**: TX leases,
PTT key-down, stable reconnect behavior, full rig controls and power
calibration, Hamlib-specific VFO exchange fallback, and fault-injection remain
unfinished. Generic `backend=hamlib` on this branch can run RX-only commands
but **must not** be selected for a live TX installation.

For development builds only, the worker path may be set as the absolute
per-rig `hamlib.worker-path` setting (default
`/usr/libexec/rustyrig/rrhamlib`). Package installations use the default.

A successful protocol framing test does not demonstrate hardware PTT safety.
Use a dummy rigctld first and do not connect a transmitter for initial
integration testing.

## Development test targets

```sh
make PROFILE=radio test-hamlib-ipc
make PROFILE=radio test-hamlib-transport
```

The second target builds a **fake worker** (not linked against Hamlib) and
tests actual `posix_spawn`, inherited descriptor 3, hello/ack, heartbeat
multiplexing, sequence-matched release acknowledgments, and worker teardown.
It requires Linux/glibc 2.34 or later for the close-from spawn file action.
Neither test proves real Hamlib CAT behavior or safe transmitter shutdown.

On a development checkout, `hamlib.worker-path` must point to an absolute
path for the compiled `bin/rrhamlib`; installed packages use libexec.

## Experimental TX: opt-in and limitations

The subprocess-backed `hamlib` backend permits key-down **only** if the
rig's configuration explicitly contains:

```ini
[rig:rig0]
backend=hamlib
hamlib.tx-enabled=true
```

The default remains `false`. The regular `rrserver` TX authorization,
interlocks, and control-room permissions still apply before invoking the
backend. The worker requires a fresh **1,500ms TX lease**, verifies
physical PTT-off before key-down, and verifies PTT-on after it keys.
A separate watchdog thread observes the deadline and keeps attempting
PTT-off if it expires; lease expiry latches the worker into refusing further
TX until that subprocess is replaced.

**Heartbeats do not renew TX.** Only an active backend polling cycle while
the proxy believes the rig keyed explicitly renews the lease. An unexpected
slow CAT transaction, stalled poll queue, or overloaded scheduler may thus
cause a legitimate transmission to cut off: this is intentional.

**Not a guaranteed hardware interlock:** Hamlib drivers can block on serial
operations, the radio can ignore CAT PTT-off, USB can disconnect, or the
host can lose power. Even a dedicated watchdog thread can be blocked by
Hamlib's internal lock. A physical TX-disable/PTT interlock is required
when a strict guarantee is needed.

The dummy rig test drives real key-down and validates:

- PTT-on rejected without a lease.
- Key-down with a valid lease, with readback confirmation.
- Periodic explicit renewals keep TX keyed while server activity continues.
- Stopping renewals causes PTT-off despite a live IPC heartbeat.
- Expired lease refuses new key-down and new lease requests.

Use `make PROFILE=radio test-hamlib-dummy` on a Hamlib-enabled host. This
is not proof of physical transmitter safety. Test with a dummy receiver
before explicitly opting a real transceiver into experimental TX.
