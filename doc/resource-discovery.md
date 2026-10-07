# Site resource discovery

Discovery commands list their own resource types in native GTK, TUI and the
browser. `/object [symbol|uuid]` inspects cached objects and properties;
`/rig` lists radios and VFOs; `/gps` lists GPS services; `/sercom` lists serial
ports; `/room list` lists rooms; `/media` lists streams and refreshes discovery.
`/rig` and `/gps` request fresh server inventory without changing subscriptions
and filter its rows in the client. GPS listings include inputs and outputs,
with effective coordinates and source when available.

UUIDs appear only for resources that have them. Serial exports, symbolic CAT
bindings and GPS inputs are names, not invented UUID objects. GPS outputs
refer to their actual gpsp or NMEA media UUID. Unknown/no-fix coordinates are omitted;
station fallback reports the effective source. Rig RX subrooms appear under
that rig; unrelated rooms remain chat-only. CAT attachments use the native
client's existing serial interface and retain the server's control checks.

| Command | Purpose |
|---|---|
| `/object [symbol\|uuid]` | Inspect cached objects and readable properties |
| `/rig [list]` | Fresh radio and VFO listing, including room requirements |
| `/rig subscribe` | Snapshot and ongoing UUID object/property updates for the site |
| `/rig unsubscribe` | Stop those property updates; does not stop media |
| `/join <room>`, `/part <room>` | Enter or leave the resource's room |
| `/media` | Refresh available stream metadata |
| `/media list` | Show cached streams and current subscriptions |
| `/media subscribe <name|uuid|#number>`, `/media unsubscribe <name|uuid|#number>` | Manage a specific stream |
| `/gps list` | List only GPS services |
| `/gps subscribe <rig-alias|station>` | Subscribe to that scope's NMEA output |
| `/gps unsubscribe <rig-alias|station>` | Stop that NMEA subscription |
| `/sercom remote` | Discover permitted server serial exports |
| `/sercom [list]` | Native local serial/PTY attachments plus permitted server serial ports; browser shows server ports |
| `/sercom attach ttyHOST0 host:ttyHOST0` | Native PTY for a named server export |
| `/sercom attach ttyGPS0 rig.gps-out` | Native logger output following the active rig |
| `/sercom disconnect ttyHOST0` | Release the attachment and remote port |

The browser can discover serial exports through `/sercom remote`, but cannot
create local PTYs. `/object` shows readable cached objects and their properties in both clients.
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

## Human references and completion

Use symbolic stream names for routine operations:

```text
/media subscribe rig0.vfo_a.rx
/media unsubscribe station.gps.rx
/rxcodec opus rig0.vfo_a.rx
/object rig0
/object rig0.A
```

`/media list` leads with stream names, descriptions, codec, room and subscription
state. Tab completion inserts symbolic names and shows descriptive labels in
GTK, TUI and the browser. Labels never become command arguments. Numeric
references and UUIDs remain available; type `#` or a UUID prefix to complete
them explicitly. Channels without a unique name fall back to UUID choices.
Names are matched case-insensitively; ambiguous names are rejected and require
an exact UUID or list number. Unknown references are rejected locally; use
`/media` to refresh discovery after a resource changes.

`/object [symbol|uuid]` inspects a rig and its immediate children, or a single
VFO such as `rig0.A`. VFO names are qualified by their owning rig so another
rig's VFO A cannot be selected accidentally. Object views show display names,
backend, readable values and units, availability, and writable flags. They use
the current property cache; `/rig subscribe` refreshes a stopped cache.
`/object` and `/gps subscribe|unsubscribe` also complete their symbolic scopes.

Symbol resolution belongs to the clients and uses current discovery metadata.
The protocol still addresses objects and streams by UUID. `/rig subscribe`
continues to subscribe to the site's object/property stream; it does not gain
a per-rig subscription argument. Browser-console `rrObjectsDump()` remains
available for the structured diagnostic view.

## Permission boundaries

Inventory and property discovery require authentication. Inventory is a
read-only snapshot and never joins a room, opens a device, starts TX, or
changes subscriptions. Stream subscriptions continue to require membership
in the advertised room. TX controls and media input retain their existing
privilege, room, PTT and ownership checks. GPS output is read-only media and
needs no physical-serial privilege. `gps-in` represents a server-configured
receiver, not permission to inject a location from a remote client.

Physical exports are omitted unless the account has `serial` or a matching
`serial.<portname>` flag, including prefixes such as `serial.ttyGPS*`.
Admin/owner status alone does not grant access. The same check applies to listing,
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
state in their owning components. Clients render and filter the records
locally; the wire inventory remains the full tree.
Command output goes to the issuing window; inventory replies retain that
destination even when the user switches windows before the response arrives.

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

## Window context

Native and browser resource listings and parameter completion use the issuing
window's context. A site room shows that site's resources and rigs; a rig room
(including its RX rooms) shows that rig and its VFOs, media, GPS and serial
services. The native status window shows everything discoverable. Other windows
without a room context also show everything. Delayed inventory replies keep
both the destination and context captured when the command was issued.

The server includes `object.room` on node and rig descriptors; VFOs inherit
their owner's room. This is descriptive metadata, not an authorization change.
Explicit `/object <symbol|uuid>` selections and named/UUID subscriptions can
still address resources outside the current listing. Media `#numbers` retain
their global positions, so filtering does not retarget an existing reference.
