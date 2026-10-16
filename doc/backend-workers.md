# Server backend workers

Each configured radio backend has one I/O worker. A slow Hamlib endpoint
therefore blocks only that radio, while the server continues servicing
WebSockets, chat, media and other rigs. Backend construction and capability
setup run during startup; ownership of runtime hardware calls transfers to
the worker before the network loop starts.

The server thread owns the rig registry, property model, connections, event
bus and protocol output. The worker owns raw backend state. Its queued jobs
carry copied command values and active-VFO snapshots. Runtime callers use
`rr_backend_api()`: reads return cached observations, writes queue work, and
poll requests coalesce per VFO. Hardware callbacks must use
`rr_backend_property_observe()`, `rr_backend_property_unavailable()` and
`rr_backend_log()` so notifications are replayed on the server thread.
They must not emit events or access mutable client/configuration state.

The main loop drains completions every cycle with a maximum 10 ms socket
poll wait. UUID property controls return their existing result after the
write and readback complete. Queue acceptance is an internal pending state,
not a new wire status. Disconnected clients are detached from pending replies.
Legacy CAT commands retain their existing receipt semantics; observations
and confirmed PTT state are published from completions.

The outstanding queue is bounded to 64 normal jobs, including undrained
results, with up to MAX_VFOS reserved release entries. Polls coalesce and
controls precede waiting polls. PTT-off goes first and cancels queued key-downs;
repeated releases can share one physical operation with separate completions.
An off/on pair on the same backend executes in order. Hamlib polling yields
between hardware calls when a control is waiting. An already-running hardware
call cannot be preempted.

`backend.hamlib-timeout-ms` defaults to 250 ms and accepts 1..5000 ms.
`hamlib.timeout-ms` in a rig section overrides it. Hamlib retries are disabled
so an unavailable endpoint cannot multiply an individual timeout. Reconnect
scheduling uses monotonic time on the worker. This is a per-call timeout,
not a deadline for a whole polling sweep or backend teardown.

Shutdown cancels waiting normal controls, runs queued releases, joins every
worker and destroys raw backend state on its worker before freeing the model.
Any transmitter still tracked as keyed receives a final release attempt.
Failed releases keep TX accounting locked and block subsequent key-down;
release retries remain permitted. The TX block still requires explicit clearing. Startup and shutdown may still wait on backend open/close.

`rrserver/tests/test_backend_workers.sh` checks thread ownership, main-thread
publication, bounded/coalesced work, release priority and shutdown. It also
holds one simulated rig indefinitely while another completes controls and
real local WebSocket ping/pong traffic stays responsive. Live multirig,
property-control, PTT and direct-media tests exercise the integrated paths.

## Optional Hamlib module

Install `rustyrig-server-hamlib` to use rigs with `backend=hamlib`. The server
loads `rrserver-hamlib.so` on demand from `path.modules` before constructing the
first Hamlib instance. No `[modules]` entry is required. Source-tree runs can
set `path.modules=./bin`; `make bin/rrserver-hamlib.so` builds the module when
`backend.hamlib` is true in the build profile. Setting that build option false
omits the module; the core server still builds without Hamlib. If Hamlib is enabled but its
development files are missing, preflight stops with an installation hint
before building; it never silently omits a configured package.
`WITH_HAMLIB=yes` requires it; `WITH_HAMLIB=no` explicitly omits it.

The base server and shared core libraries do not link libhamlib. Debian keeps
the Hamlib runtime dependency in the optional package, which depends on the
matching server version. The shipped configuration enables only `rig0`; add
`rig1` to `rig.instances` after installing the module to enable the example
Hamlib NET rigctl rig. Existing `backend=hamlib` settings remain valid.

A missing module produces an installation/path diagnostic instead of silently
substituting another backend. Hamlib follows the usual worker ownership rules.
The module refuses unloading while a rig still owns its backend callbacks;
shutdown joins the workers and frees backend instances before unloading it.
Unloading unregisters the type so a later load can register it again.
