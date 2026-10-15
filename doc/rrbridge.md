# External receiver bridges

`rrserver` owns radios, authorization, rooms and client-visible properties.
Each enabled bridge is a separate `rrbridge` process with one driver loaded
through the existing module loader. Shared configuration, the versioned driver
ABI, and local control IPC live in `librrprotocol/bridge.*`.

This milestone implements registration and control, demonstrated end-to-end
with a test driver. The Kiwi module remains a formatter/probe: it does not
connect or authenticate to a Kiwi receiver and stays offline. No bridge audio,
waterfall, IQ or transmission is implemented.

## Configuration

```ini
[bridge]
instances=hf-east
path=auto
modules=auto

[bridge:hf-east]
enabled=true
driver=kiwi
host=receiver.example.net
port=8073
rig=hf-east
```

`path=auto` searches `PATH` for an executable `rrbridge`, then tries
`./bin/rrbridge`. The old shipped `path=bin/rrbridge` also uses this discovery.
Other explicit paths are honored; use `./bin/rrbridge` to force the local build.
`modules=auto` prefers the installed driver in
`/usr/lib/rustyrig/modules/rrbridge`, then `./bin`; the old `modules=bin` is
also automatic. Other explicit module directories are honored.

The bridge roster is opt-in and empty in the shipped configuration. `enabled`
accepts true/false or 1/0. Bridge/driver names and rig aliases contain only
letters, digits, underscores or dashes and must fit in 63 bytes. Duplicate
rig aliases, including collisions with physical radios, fail startup.
Driver-specific settings remain in the configuration file and are available
through the host's `setting` callback; credentials never enter the command line.

An enabled bridge adds an ordinary backend `bridge` rig with one persistent
VFO A. UUIDs use the existing database identity mechanism. Its alias is the
configured `rig` or the bridge name. The server assigns an unused numbered
`#station-rigN` room, preserving existing room authorization and client routing.
Room numbers are stable for the same configuration/order, while UUIDs remain
stable independently of process restarts. Leave `rig.default` pointing to an
existing radio, or explicitly select a configured bridge alias. Do not also
list a bridge alias in `rig.instances`; the bridge roster adds it.

`bin/rrbridge --config FILE --bridge NAME --probe` validates module loading and
capability declarations without claiming an external receiver connection.

## State and properties

A configured logical radio exists while its process is disconnected. After
HELLO and validated capabilities, VFO descriptors expose `frequency` (long Hz,
minimum/maximum, step 1) and `mode` (canonical RustyRig mode names in an enum).
The generic explicit same-type schema-refresh API updates those descriptors
when capabilities change across a bridge restart. Capability values and enum
strings are copied into the server model.

Rig properties distinguish `rx.supported`/`tx.supported` from `rx.available`,
`tx.available`, and `online`. Media support declarations use
`rx.audio.supported`, `rx.iq.supported`, and `rx.waterfall.supported`.
`online` and `rx.available` become true only on a valid driver state report,
not module load, process launch, HELLO or capability exchange. Disconnects mark
frequency and mode observations unavailable while retaining their last values.
TX availability stays false and there are no bridge PTT or TX media operations.
Client permissions and PTT interlocks remain under existing server authority.

Existing native and browser object/property discovery consumes these standard
schemas and observations. No Kiwi-specific client messages or UI are needed.
The control-only milestone does not provision bridge media channels or attach
local sound-card pipelines to an external receiver.

## Driver and local IPC contracts

Driver ABI version 3 adds nonblocking `start`, `poll`, and `set_receiver`
operations. External SDR networking belongs to the driver. `start` returning
true means startup was accepted; it does not mean the receiver is connected.
Callbacks run on the host thread. `state` reports actual connection, frequency
and mode; `complete` acknowledges a matching accepted command after apply and
readback. A false `set_receiver` return rejects immediately and must not also
complete the same request. Driver-owned capability strings are copied before
sending. Mode names use the canonical spellings understood by `vfo_parse_mode`.

Local IPC version 2 uses a bounded fixed record over inherited FD 3, created
with `SOCK_SEQPACKET`. Records are same-host/same-build, not a portable network
serialization format. Public APIs expose no Mongoose types. Records validate
size, magic/version, type, reserved fields, strings, capability flags/ranges,
and request IDs. The server validates both configured bridge and driver
identity and requires HELLO before capabilities before state/control traffic.
The inherited socket is local authority; it is not a server-link peer.

Messages are HELLO, CAPABILITIES, SET (frequency and mode, request ID), STATE,
RESULT, HEARTBEAT and SHUTDOWN. There is one outstanding command per receiver;
additional commands fail explicitly through the existing backend-failure result.
No PCM enters this queue. Future media requires a separately bounded transport.
Nonblocking send failure closes the session instead of growing a hidden queue.

The server processes at most eight records per bridge per iteration. Handshake
and control deadlines are five seconds; missing heartbeats expire at ten.
A failed process gets exponential retry delays of 1–64 seconds, reset only
after a stable 30-second session. A wedged process is killed after a two-second
termination grace period. Normal server shutdown first requests driver cleanup
and then falls back to the existing subprocess stop mechanism.

Only actual driver observations update client-visible tuning. A successful
acknowledgment retains desired frequency/mode in server memory for restoration
after bridge/remote reconnects; failed requests preserve the prior desired
state. This does not add tuning persistence across a full server restart.

## Kiwi scope and next milestone

The current Kiwi declarations cover a conservative 0–30 MHz baseband and USB,
LSB, AM and CW subset. The formatter's kHz frequency units and mode-specific
passbands were checked against
[kiwiclient's client.py](https://github.com/jks-prv/kiwiclient/blob/master/kiwi/client.py).
Configured frequency offsets and dynamic receiver/firmware restrictions must
be negotiated by the future live driver before reporting availability. The
subset is not a claim that the remote device has been queried.

Next implement Kiwi WebSocket authentication, tuning/state confirmation and
reconnects inside the Kiwi driver, then a separate PCM transport into the
existing server/DSP media path. Preserve this control and process isolation.

## Validation

`tests/run-tests.sh rrbridge rrserver librrprotocol` discovers component suites.
New tests cover ABI rejection, IPC framing/validation, supervisor handshake and
queue bounds, and a real server + bridge + loaded asynchronous test driver.
The live fixture exercises ordinary object discovery/descriptors, acknowledged
frequency/mode requests, failed/out-of-range controls, offline Kiwi, room-number
collisions, child crashes, wedged-command timeouts and desired-state restoration.
