# Site resource discovery

`/rig list` requests a fresh tree without changing subscriptions. Native GTK,
TUI and the browser show the same server-supplied hierarchy:

```text
site station-name  uuid=... room=#station-name
+- room #station-name  service=chat
+- gps station.gps-in  state=receiver
+- gps station.gps-out  uuid=... coordinates=...
+- serial ttyHOST0  service=serial access=serial.ttyHOST0|admin
+- rig rig0  uuid=... backend=internal room=#station-name-rig0
   +- room #station-name-rig0  service=TX-control
   +- cat rig0.cat
   +- vfo A  uuid=... frequency=14074000 Hz
      +- media rig0.vfo_a.rx  uuid=... direction=RX codec=opus
   +- gps rig0.gps-in  state=disabled-by-fixed-position
   +- gps rig0.gps-out  uuid=... coordinates=38.1234567,-80.7654321
```

UUIDs appear only for resources that have them. Serial exports, symbolic CAT
bindings and GPS inputs are names, not invented UUID objects. GPS outputs
refer to their actual NMEA media UUID. Unknown/no-fix coordinates are omitted;
station fallback reports the effective source. Rig RX subrooms appear under
that rig; unrelated rooms remain chat-only. CAT attachments use the native
client's existing serial interface and retain the server's control checks.

| Command | Purpose |
|---|---|
| `/rig list` | Fresh resource tree, including subscription state and room requirements |
| `/rig subscribe` | Snapshot and ongoing UUID object/property updates for the site |
| `/rig unsubscribe` | Stop those property updates; does not stop media |
| `/join <room>`, `/part <room>` | Enter or leave the resource's room |
| `/media` | Refresh available stream metadata |
| `/media list` | Show cached streams and current subscriptions |
| `/media subscribe <uuid>`, `/media unsubscribe <uuid>` | Manage a specific stream |
| `/gps list` | Discover GPS services within the site tree |
| `/gps subscribe <rig-alias|station>` | Subscribe to that scope's NMEA output |
| `/gps unsubscribe <rig-alias|station>` | Stop that NMEA subscription |
| `/sercom remote` | Discover permitted server serial exports |
| `/sercom list` | Native local serial/PTY attachments |
| `/sercom attach ttyHOST0 host:ttyHOST0` | Native PTY for a named server export |
| `/sercom attach ttyGPS0 rig.gps-out` | Native logger output following the active rig |
| `/sercom disconnect ttyHOST0` | Release the attachment and remote port |

The browser can discover serial exports through `/sercom remote`, but cannot
create local PTYs. Existing `/objects` dumps the native property cache.
`/rig subscribe` refreshes that cache after unsubscribing; ordinary login
already subscribes to property updates.

Native `/help` lists the registered client commands, hiding administrative
commands from users without admin/owner privileges. Browser `/help` is
maintained separately and includes the discovery and subscription commands
above; local serial/PTY attachment commands require the native client.

Audit events are stored in the server's SQLite `audit_log` table and can be
read on the server with `tools/rr-get-audit-log`. Client audit replay and a
`/audit` command are not implemented. Automatic chat replay is a separate
feature and does not replay audit events.

## Permission boundaries

Inventory and property discovery require authentication. Inventory is a
read-only snapshot and never joins a room, opens a device, starts TX, or
changes subscriptions. Stream subscriptions continue to require membership
in the advertised room. TX controls and media input retain their existing
privilege, room, PTT and ownership checks. GPS output is read-only media and
needs no physical-serial privilege. `gps-in` represents a server-configured
receiver, not permission to inject a location from a remote client.

Physical exports are omitted unless the user satisfies `[serial:name] access`
(or `serial.access`, default `admin|owner`). The same check applies to listing,
opening, configuring, and binary transfer; revoked access closes the tunnel.
Only symbolic server names travel over the wire. Server device paths are
neither listed nor accepted from clients, including a `serial.path` supplied
alongside a valid name. Client-local device paths remain client configuration.

## Wire extension

Send `msg.type=object`, `object.cmd=inventory`, and a bounded `request.id`.
The response is ordered `object.cmd=inventory-entry` records, followed by
`object.cmd=inventory-end`, all carrying that request ID. Entries contain
`inventory.kind`, `name`, `depth` (site=0, rig=1, VFO=2, VFO media=3), and
optional `uuid`, `room`, `backend`, `frequency`, `codec`, `direction`,
`subsystem`, `coordinates`, `source`, `service`, `state`, `access`, `action`.
This snapshot is independent of UUID stream epochs, sequence numbers and the
object/property cache. Clients must not feed it into that cache. Existing
`object.cmd=snapshot/unsubscribe` retain their subscription semantics.

Server components contribute through `server.inventory.collect` event
listeners, using the requested scope and depth. This keeps GPS and serial
state in their owning components. Clients render the records locally.

## Checks to run

After building, focused checks are:

```sh
bash rrserver/tests/test_gps.sh
bash rrserver/tests/test_gps_live.sh
bash rrserver/tests/test_serial_live.sh
bash rrserver/tests/test_serial_tunnel.sh
bash rrclient/tests/test_completion.sh
node www/tests/resource_discovery.js
```

The live checks use temporary servers and databases. They check UUID/GPS
discovery, coordinate formatting, room-gated subscriptions, serial permissions,
path rejection, and lossless binary serial transport.
