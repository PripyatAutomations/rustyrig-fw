# External receiver bridges

`rrserver` owns radios, authorization, rooms and client-visible properties.
Each enabled bridge is a separate `rrbridge` process with one driver loaded
through the existing module loader. Shared configuration, the versioned driver
ABI, and local control IPC live in `librrprotocol/bridge.*`.

The Kiwi driver connects to a receiver over WebSocket, authenticates, configures
mono audio, confirms tuning changes and forwards received sound to clients.
A separate bounded PCM socket and `fwdsp` resampler preserve DSP/process
isolation. Waterfall, IQ, ADPCM, signal-strength publication and transmission
are not implemented. The driver requests uncompressed PCM explicitly.

## Configuration

```ini
[bridge]
instances=hf-east
path=auto
modules=auto

[bridge:hf-east]
enabled=true
driver=kiwi
host=hutton.proxy.kiwisdr.com
port=8073
rig=hf-east
frequency=14074000
mode=USB
; password=optional-receiver-password
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
`host` is a DNS hostname or IPv4 address, `port` defaults to 8073, and
`frequency`/`mode` select initial tuning (14074000 Hz / USB by default).
The initial transport is plain WebSocket as used by the receiver's HTTP
endpoint. Passwords must fit 128 bytes and contain no whitespace. Global
`[bridge]` host/port/password/frequency/mode provide defaults for named sections.
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
Bridge receivers expose RX audio channels through the existing media protocol.
They never provision TX channels or attach local sound-card capture pipelines.

## Driver and local IPC contracts

Driver ABI version 3 adds nonblocking `start`, `poll`, and `set_receiver`
operations. External SDR networking belongs to the driver. `start` returning
true means startup was accepted; it does not mean the receiver is connected.
Callbacks run on the host thread. `state` reports actual connection, frequency
and mode; `complete` acknowledges a matching accepted command after apply and
confirmation. A false `set_receiver` return rejects immediately and must not also
complete the same request. Driver-owned capability strings are copied before
sending. Mode names use the canonical spellings understood by `vfo_parse_mode`.

Local IPC version 3 uses a bounded fixed record over inherited FD 3, created
with `SOCK_SEQPACKET`. Records are same-host/same-build, not a portable network
serialization format. Public APIs expose no Mongoose types. Records validate
size, magic/version, type, reserved fields, strings, capability flags/ranges,
and request IDs. The server validates both configured bridge and driver
identity and requires HELLO before capabilities before state/control traffic.
The inherited socket is local authority; it is not a server-link peer.

Messages are HELLO, CAPABILITIES, SET (frequency and mode, request ID), STATE,
RESULT, HEARTBEAT and SHUTDOWN. There is one outstanding command per receiver;
additional commands fail explicitly through the existing backend-failure result.
No PCM enters this queue. Inherited FD 4 carries separate `SOCK_SEQPACKET`
media records: 12-byte header (`RRBA`, version 1, mono-S16LE format 1, two
reserved zero bytes, big-endian uint32 sample rate), followed by little-endian
signed 16-bit samples. Rates are 8000–48000 Hz, with 1–2048 samples per record.
Send is nonblocking; a full audio socket drops a complete record. The server
bounds each poll to eight media records and keeps control on its separate FD.
`fwdsp -M resample` converts native mono PCM to canonical 16 kHz in a separate
process before the existing per-channel encoder fanout. Failed resamplers are
recreated as audio arrives; disconnects stop the receiver's resampler.
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

## Kiwi wire behavior and limits

The SND WebSocket URL, authentication, rate negotiation, uncompressed mono
sample framing, endianness and mode-specific passbands follow
[kiwiclient](https://github.com/jks-prv/kiwiclient/blob/master/kiwi/client.py).
The NEW_FREQ flag (0x04) in
[Kiwi firmware](https://github.com/jks-prv/Beagle_SDR_GPS/blob/master/rx/rx_sound.cpp)
is emitted after applying frequency/mode; the bridge waits for that indication
before marking initial reception online or completing a changed tuning request.
An unchanged frequency/mode completes without another hardware operation.
Kiwi does not echo a numeric tuning readback or request ID. Reported tuning
therefore means the requested tuning was confirmed by that device flag, not an
independent measured frequency. USB, LSB, AM and CW use fixed mode passbands.

The initial range is a conservative 0–30 MHz baseband. Nonzero receiver
frequency offsets are rejected rather than silently mistuning. Unsupported
stereo/IQ or compressed streams fail closed. A short compressed startup tail
is ignored while the uncompressed setting takes effect. Frames are length
checked, sequences must advance, receive/send buffering is bounded, and raw
peer metadata/passwords are not copied to logs.

The driver logs busy/maintenance/redirect/authentication failures separately.
Kiwi's `too_busy` message can mean channel capacity or an external-client
(API) limit. Upstream firmware classifies external clients after ten seconds
and may then send `too_busy` and close an already streaming connection; see
[`called_every_second()` in support/stats.cpp](https://github.com/jks-prv/Beagle_SDR_GPS/blob/master/support/stats.cpp).
The message does not distinguish these causes. A short burst of audio followed
by rejection therefore does not establish a decoder or transport failure.
Use a receiver whose owner permits API clients and has API capacity available;
the bridge respects receiver admission limits.
Its exponential reconnect delay resets only after 30 seconds of continuous
reception, so briefly admitted streams do not trigger rapid retry loops.
The driver reconnects with bounded exponential backoff, retains confirmed
tuning, sends keepalives, and times out missing audio or unconfirmed commands.
The host refreshes its process `time_t now` before startup and on every poll,
so bridge/module logging has current timestamps.

A live end-to-end smoke test against `hutton.proxy.kiwisdr.com:8073` succeeded:
authentication, frequency and mode changes, native PCM reception, resampling,
and audio frames delivered to an authenticated local RustyRig client. That
receiver was released after the check. No long-duration playback guarantee is
implied by this short test.

Next extend passband controls, signal measurements, additional firmware/range
negotiation and compressed audio if needed; keep device-specific behavior in
the Kiwi driver and retain the separate DSP/media paths.

## Validation

`tests/run-tests.sh rrbridge rrserver librrprotocol` discovers component suites.
New tests cover ABI rejection, IPC framing/validation, supervisor handshake and
queue bounds, and a real server + bridge + loaded asynchronous test driver.
The bridge lifecycle fixture exercises ordinary object discovery/descriptors, acknowledged
frequency/mode requests, failed/out-of-range controls, offline Kiwi, room-number
collisions, child crashes, wedged-command timeouts and desired-state restoration.

`rrserver/tests/test_kiwi_live.sh` runs an isolated wire-level Kiwi peer, a real
server, bridge and DSP workers. It covers authentication rejection/retry,
NEW_FREQ-confirmed tuning, both PCM byte orders, resampled client audio and
remote disconnect/reconnect. Protocol tests cover media framing and
backpressure, and DSP tests check that 12-to-16 kHz conversion preserves duration.
