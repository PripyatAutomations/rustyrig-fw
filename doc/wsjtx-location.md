# WSJT-X transmitter location from rrclient

The rrclient WSJT-X integration is enabled by default and listens on
**127.0.0.1:2237/UDP**. It receives WSJT-X heartbeat/status packets and
sends WSJT-X NetworkMessage **Location (type 11)** replies containing a
6-character Maidenhead square for the **selected transmitting rig**.

Enable UDP Reporting in WSJT-X **Settings → Reporting** and set:
- UDP server: `127.0.0.1`
- UDP server port: `2237`

When WSJT-X sends its next heartbeat/status, rrclient learns its UDP
source address and unique session ID and replies with the currently cached
grid, if available. Rig changes and fresh GPS fixes update that grid.
Multiple instances using different UDP source ports/IDs are supported.

In `/etc/rustyrig/rrclient.cfg` under `[general]`:

```ini
wsjtx.enabled=true
wsjtx.udp.port=2237
```

The socket binds to loopback, never a public network interface. If
another application (e.g. GridTracker) already binds 2237, choose a
different port in both rrclient and WSJT-X. The station coordinate comes
from RustyRig's selected rig GPS/NMEA stream, including manually
configured remote transmitter locations; it never falls back to the
computer's location. Unknown or invalid fixes are not sent.

WSJT-X Location updates are temporary for the session and do not change
its persistent configuration. The integration uses GTK-independent GLib
events and works with the native client frontends. The source code is
`rrclient/wsjtx.c`.
