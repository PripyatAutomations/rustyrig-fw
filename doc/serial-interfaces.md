# Serial endpoints and rig GPS for loggers

The server owns the rig's location. Native client PTYs expose CAT, server serial
ports, and generated GPS data to local logging programs. These services work in
both GTK and TUI; their transport and routing live in common client code.

## Symbolic bindings

Server configuration:

```ini
[general]
serial.access=admin|owner
station.gps.position=38.1234567,-80.7654321
path.modules=/usr/lib/rustyrig/modules/rrserver

[rig:rig0]
gps.position=40.5,-75.25

[serial]
ttyHOST0=serial:/dev/ttyUSB0@115200,8n1
ttyGPS0=station.gps-in@4800
ttyGPS1=rig1.gps-in@4800
ttyGPS2=rig0.gps-out@4800

[serial:ttyGPS1]
type=serial
path=/dev/ttyUSB1

[modules]
rrserver-gps-nmea=
;rrserver-gpsd=
```

Client configuration:

```ini
[serial]
ttyCAT0=rig0.cat@38400
ttyCAT1=rig1.cat@38400
ttyHOST0=host:ttyHOST0
ttyGPS0=rig.gps-out@4800
;ttyGPS1=rig1.gps-out@4800
;ttyGPS2=station.gps-out@4800

[serial:ttyHOST0]
buffer-bytes=65536
```

A local endpoint defaults to a PTY at `./dev/<name>`. `type=serial` and `path`
select a real local serial device instead. Without a mapping, the native client
creates `ttyCAT0=rig0.cat` at 9600 baud. `none` disables a mapping. Existing
`cat.pty.enable=false` disables the default CAT endpoint; `cat.pty.path` remains
the fallback when `serial:ttyCAT0.path` is absent. GPS endpoints default to 4800.

Inline `@baud,mode` overrides the endpoint section's `baud` and `mode`; either
part can be omitted (`@38400`, or the older `@8n1`). Mode is data bits, parity,
and stop bits, such as `8n1`, `7e1`, or `8n2`. Defaults are 8N1. Standard baud
rates through 115200 are supported, with faster rates where the platform
provides them. A client `host:` binding inherits the server's initial settings
unless explicitly overridden. Clients may request only a symbolic export name;
absolute remote paths and `serial.path` requests are rejected. Local device
paths in client endpoint settings refer only to hardware on the client.

## Discovery and permissions

`/rig list` shows GPS services, their effective coordinates and media UUIDs,
plus permitted serial exports. `/sercom remote` lists server serial exports;
`/sercom list` lists local attachments. `/gps subscribe rig0` and
`/gps unsubscribe rig0` manage the NMEA media subscription. Join the advertised
room first. A logger attaches with `/sercom attach ttyGPS0 rig.gps-out` to
follow the selected rig or with `rig0.gps-out` to pin one rig.

Set `[serial:ttyHOST0] access=serial.ttyHOST0|admin|owner` to grant that
physical export to accounts carrying any of those privileges. Unspecified
exports use `serial.access` (default `admin|owner`). Discovery, open, settings,
and binary I/O enforce the same check; revocation closes an existing tunnel.
GPS emulation uses room-scoped read-only media and requires no serial privilege.
Server GPS inputs are configured receiver services, not client write endpoints.

## Runtime management

```text
/sercom list  # Local attachments and available server ports
/sercom attach ttyCAT1 rig1.cat@38400
/sercom attach ttyHOST0 host:ttyHOST0
/sercom attach ttyGPS0 rig.gps-out@4800
/sercom attach radioUSB rig1.cat /dev/ttyUSB1
/sercom disconnect ttyHOST0
```

The optional device argument opens a real client-side port. Disconnect before
changing an existing binding. Commands affect the current client process;
edit `[serial]` to retain mappings across restarts. Closing removes only the
PTY symlink owned by this process. Existing files and links are not overwritten.
Real device access uses each process's operating-system account.

CAT endpoints parse semicolon-delimited Yaesu commands. Replies go only to the
requesting port. Reads use the named rig's UUID property cache; writes target
its authoritative control room and remain subject to privileges, membership,
and PTT interlocks. Endpoint `vfo=A` is the default selector; explicit FA/FB
commands still select A/B. Join the rig's base room before controlling it.

## Server serial passthrough

`serial:/dev/...` exports are allowlisted, exclusively owned by one authenticated
WebSocket session, and require `serial.access` (default `admin|owner`). A per-port
`access` overrides that policy. Different aliases cannot open the same active
physical device. The server opens it only after an authorized attachment,
restores its prior termios settings on close, and releases it when the session
ends. Reopening starts with its configured baud/mode again.

Binary payloads are arbitrary bytes in MODEM/`seri` frames. They bypass CAT and
GPS parsing. Baud/data/parity/stop changes made by a logger to the client PTY
are detected and applied to the server device. PTY drivers may reject modes
that physical serial hardware supports; the initial remote mode is retained
in that case. PTYs do not expose physical DTR/RTS, modem status, or break control,
so those signals are not transported by this implementation.

`buffer-bytes` is bounded at both ends: client default 65536 for passthrough
(8192 for other services), server default 16384. Values are 1024 through
1048576; `0` retains one 1024-byte transfer block. Partial writes are queued;
one block per direction stays in flight until acknowledged. Full buffers apply
backpressure rather than silently dropping raw serial bytes. The physical
sender must honor any flow control its own device requires; a WebSocket cannot
prevent overflow inside external hardware. NMEA outputs have bounded queues
and log a full queue rather than growing indefinitely.

## Rig location and GPS adapters

`gps.position=latitude,longitude` in `[rig:rigN]` is the authoritative fixed
position for that rig. `station.gps.position` in `[general]` is the site fallback.
Coordinates are signed decimal degrees, with up to seven fractional digits,
latitude within ±90 and longitude within ±180. Invalid coordinates reject
startup. Empty/absent coordinates enable live inputs for that source.

The location priority is:

1. The rig's configured coordinates; its live GPS input is disabled.
2. The rig's own GPS receiver, when configured.
3. The station's configured coordinates; the station receiver is disabled.
4. The station GPS receiver.

A rig with its own receiver does not inherit another location when that receiver
has no fix. NMEA no-fix reports generate invalid-position output. A receiver's
last accepted position is otherwise retained until another position/no-fix
report arrives; this is a logger-location service, not a navigation or time
synchronization service.

`rrserver-gps-nmea.so` consumes `station.gps-in` and `rigN.gps-in` bindings from
the generic serial manager, whether PTY or real port. Input is newline-delimited
NMEA with a valid XOR checksum. Oversize/invalid records are discarded and
framing resumes at the next terminator. Configured coordinates prevent that
source's input port from opening.

`rrserver-gpsd.so` connects asynchronously to `gpsd.url` (default
`tcp://127.0.0.1:2947`), reconnects after failures, and feeds the source named by
`gpsd.target` (default `station`, or a rig alias). Optional `gpsd.device` selects
one receiver. It requests NMEA WATCH output, including conversions from binary
receivers; see the [gpsd protocol](https://gpsd.io/gpsd_json.html). Configured
coordinates disable connection to gpsd for that target.

The server extracts RMC/GGA/GLL positions using integer arithmetic. The default `gpsp` stream
carries the effective position and valid/manual flags; each client synthesizes
checksum-correct RMC with its current UTC. Configured coordinates use manual mode
`M`; receiver positions use automatic mode `A`; no-fix output uses status `V`/mode
`N`. The generated position changes to the selected radio's coordinates when the
operator switches rigs; it does not report the operator's computer location or
interpolate a journey between rigs. Server-side serial GPS outputs use the same
`librustyaxe` NMEA generator.

Server `gps-out` endpoints default to synthesized position RMC. Set
`[serial:ttyGPS2] gps-output=nmea` to forward every checksum-valid sentence
from that endpoint's effective receiver, including GGA, GSA, GSV and other
records. Multiple outputs may follow the same station or rig independently.
`[general] gps.output=position` sets the default (`nmea` is also supported).
Fixed sources still synthesize RMC in NMEA mode. Rigs inheriting the station
also inherit its complete receiver stream; rigs with their own input remain
independent. For native client ports, use `ttyNMEA0=rig.nmea-out@4800`,
`rigN.nmea-out`, or `station.nmea-out` to request complete receiver data.
These ports subscribe to separate read-only MODEM/`nmea` channels named
`station.nmea.rx` or `rigN.nmea.rx`; ordinary `gps-out` ports subscribe only
to `gpsp`. Each NMEA frame contains one checksum-valid sentence without CRLF;
the native serial writer adds CRLF. Full receiver data crosses the network
only for explicit NMEA subscribers. The browser can explicitly subscribe via
`/media` and emits the same `rustyrig:gps-nmea` event for raw frames.

Position snapshots are sent immediately to a new media subscriber, whenever
the effective coordinates or valid/manual flags change, and every
five minutes as a refresh. Repeated receiver sentences with unchanged position
do not cause `gpsp` network traffic.
Switching the native client's active rig replaces its automatic GPS subscription
and therefore updates its logger immediately without updating other operators.
`rig.gps-out` follows the active rig (or the station in the lobby).
`rigN.gps-out` stays pinned to that rig; `station.gps-out` stays pinned to the site.
Bare client `gps-out` remains a shorthand for the active rig. A logger opens the
resulting `./dev/ttyGPS0` just as it would a GPS serial device.

GPS channels are read-only MODEM/`gpsp` media channels named `station.gps.rx`
and `rigN.gps.rx`, with no VFO. Each 9-byte payload contains signed big-endian
int32 latitude/longitude in 1e-7 degrees and valid/manual flags. Rig channels use
their rig UUID/index and room; station uses the site lobby and no rig. Join the
relevant rig base/RX room before subscribing. Native clients automatically
subscribe when a matching `gps-out` endpoint exists. Browser subscriptions are
explicit; accepted position frames are converted to RMC and emit
`rustyrig:gps-nmea` with `nmea`, `rig`, and `stream` fields. The browser cannot
create operating-system PTYs.

Adapters feed the server `gps.nmea.input` event with `gps.source`/`gps.nmea`
JSON fields. Server serial outputs consume `serial.gps.position` events with
`gps.source`, signed `gps.lat`/`gps.lon` (1e-7 degrees), and `gps.flags`; the
shared `librustyaxe` helper formats the local RMC sentence. Native clients emit
`serial.gps.output` with `gps.source`, `gps.nmea`, and `gps.selected` for routing
to the active rig's logger.

## Serial wire format

Control uses `msg.type=serial` JSON with `serial.cmd`:
`list`, `open`, `configure`, `close`, and `read`. An open includes the client
endpoint `name` and server export `port`; optional `baud`/`mode` override its
initial settings. Replies are `available`, `opened`, `configured`, `closed`,
`written`, or `error`, including the endpoint name and session stream where
applicable. Only the owning session can configure/write/close its export.

MODEM/`seri` binframes use the existing 28-byte header, no rig/VFO (`255`),
a nonzero session-local stream, and 1–1024 raw payload bytes. TX means client
to device; RX means device to client. Sequences start at 1 in each direction.
The server sends `written` only after the whole TX block is written; the client
sends `read` with stream/sequence after its RX block drains to the local PTY.
Streams are never reused within a connection, preventing delayed frames from
reaching a newly attached device. Reconnect after exhausting its 255 streams.
Queued bytes are discarded on connection loss rather than replayed into a new
session. GPS MODEM/`gpsp` frames use media subscription streams separately,
RX direction, no VFO, and the fixed 9-byte position record described above.
