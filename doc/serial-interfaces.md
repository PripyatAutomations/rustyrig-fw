# Native serial interfaces

The common native client manages serial endpoints for both GTK and TUI. Each
endpoint has a transport (PTY or real serial device) and a service binding.
Transport code in `rrclient/serial.c` owns descriptors, GLib watches, raw serial
settings, and bounded asynchronous writes. Services in `rrclient/sercom.c` own
framing and routing. Other services can use the same transport callbacks without
adding behavior to the CAT parser or a frontend.

```ini
[serial]
ttyCAT0=rig0.cat
ttyCAT1=rig1.cat
ttyGPS0=gps-in
ttyGPS1=gps-out

[serial:ttyCAT0]
type=pty
path=./dev/ttyCAT0
baud=9600

[serial:ttyGPS0]
type=serial
path=/dev/ttyUSB0
baud=4800
```

Without a mapping, `ttyCAT0` defaults to `rig0.cat`. Other PTYs default to
`./dev/<name>`. An endpoint mapped to `none` is disabled. Existing
`cat.pty.enable=false` still disables the default endpoint, and `cat.pty.path`
is honored when `serial:ttyCAT0.path` is absent. Configuration takes effect at
client startup. Real serial devices use raw 8N1; supported baud rates are 1200,
2400, 4800, 9600, 19200, 38400, 57600, and 115200. GPS services default to 4800;
CAT defaults to 9600. CAT commands without an explicit selector use the
endpoint's `vfo` setting (default `A`); FA/FB still explicitly select A/B.
Device access uses the client's operating-system account.

Runtime management is shared by GTK and TUI:

```text
/sercom list
/sercom attach ttyCAT1 rig1.cat
/sercom attach radioUSB rig1.cat /dev/ttyUSB1
/sercom disconnect ttyCAT1
```

The optional device argument opens a real serial port. Without it, the named
endpoint's configured transport is used. Disconnect closes the transport and
removes its owned PTY link. Disconnect before changing an existing binding;
a failed attachment leaves existing endpoints intact. These commands manage
runtime bindings; edit `[serial]` to retain them across restarts. Existing files
and another client's links are never overwritten. A disconnected physical
serial device can be reopened with disconnect followed by attach.

A CAT endpoint parses semicolon-delimited Yaesu commands. Replies return only
to the requesting port. Reads use the named rig's UUID property observations
and room-scoped PTT status, independent of the selected GUI tab. Writes carry
that rig's primary room and remain subject to server privileges, room membership,
and PTT interlocks. Join the rig's base room (or add it to the client's autojoin
list) before controlling it. Multiple endpoints do not enable simultaneous
transmission through the server's existing single-talker arbitration.

`gps-in` accepts newline-delimited NMEA sentences with valid hexadecimal XOR
checksums. Oversized frames are discarded through the next terminator. Accepted
sentences emit the common-client `serial.gps.input` event and are forwarded to
attached `gps-out` endpoints. Programs interested in GPS data subscribe with
`event_on()`; the payload is the complete sentence without its line ending.
A producer can emit `serial.gps.output` with the same payload to write valid
sentences to all output endpoints. Output uses CRLF endings. This provides
NMEA transport and forwarding; it does not calculate navigation fixes or invent
GPS sentences.

Browser `/sercom` explains that local PTYs and serial devices are managed by the
native client. Serial-device ownership is frontend-specific; CAT wire routing
and the server's room authorization remain authoritative C behavior.
