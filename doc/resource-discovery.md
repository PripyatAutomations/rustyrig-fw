# Site resource discovery

Discovery commands list their own resource types in native GTK, TUI and the
browser. `/object` lists the complete site inventory; `/object <symbol|uuid>`
inspects cached objects and properties;
`/rig` lists sites with radio and VFO subtrees; `/gps` lists GPS services; `/sercom` lists serial
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
| `/object` | Fresh complete resource tree |
| `/object <symbol\|uuid>` | Inspect cached objects and readable properties |
| `/rig [list]` | Fresh radio and VFO listing, including room requirements |
| `/rig subscribe` | Snapshot and ongoing UUID object/property updates for the site |
| `/rig unsubscribe` | Stop those property updates; does not stop media |
| `/join <room>`, `/part <room>` | Enter or leave the resource's room |
| `/media` | Refresh available stream metadata |
| `/media list` | Show cached streams and current subscriptions |
| `/media subscribe <name|uuid|#number>`, `/media unsubscribe <name|uuid|#number>` | Manage a specific stream |
| `/gps list` | List GPS services under their site and rig parents |
| `/gps subscribe <rig-alias|station>` | Subscribe to that scope's NMEA output |
| `/gps unsubscribe <rig-alias|station>` | Stop that NMEA subscription |
| `/sercom remote` | Discover permitted server serial exports |
| `/sercom [list]` | Native local serial/PTY attachments plus permitted server serial ports; browser shows server ports |
| `/sercom attach ttyHOST0 host:ttyHOST0` | Native PTY for a named server export |
| `/sercom attach ttyGPS0 rig.gps-out` | Native logger output following the active rig |
| `/sercom disconnect ttyHOST0` | Release the attachment and remote port |

The browser can discover serial exports through `/sercom remote`, but cannot
create local PTYs. `/object <symbol|uuid>` shows readable cached properties in both clients.
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

Native and browser `/rig` listings show every site advertised by the connected
server, including linked sites, with rig and VFO children at the server-provided
depths. They are not restricted to the current chat room. `/gps` retains site
and rig parents around GPS services; browser serial inventory does likewise for
permitted serial exports. Parent rows count toward the displayed entry total.
The server currently advertises linked site/rig/VFO inventory, but does not
advertise linked GPS or serial services. Empty site or rig branches may appear
in service-specific trees. Discovery does not imply permission to control a
resource; the existing room and subscription checks still apply.

Delayed inventory replies keep the destination captured when the command was
issued. Cached `/object` and `/media` listings and parameter completion retain
their current window filters. Native `/sercom` uses the separate server serial
export list, whose entries currently belong to the connected server only.

The server includes `object.room` on node and rig descriptors; VFOs inherit
their owner's room. This is descriptive metadata, not an authorization change.
Explicit `/object <symbol|uuid>` selections and named/UUID subscriptions can
still address resources outside the current listing. Media `#numbers` retain
their global positions, so filtering does not retarget an existing reference.

## Node Statistics / Performance

Cluster Health appears as a root-level GTK Explorer branch beside This Client and the connected servers. It indexes directly connected and linked site nodes from existing inventory and lets you double-click a node to jump to its Statistics / Performance folder. Reporting means performance entries are present without detected threshold violations; it is not a guarantee that every subsystem is healthy. Warning includes unavailable performance entries, event deferred-handler averages of 10 ms or queue waits of 20 ms, and any catalog ACK timeout during the sample. Critical is shown for deferred-handler averages of 50 ms, queue waits of 100 ms, or five catalog ACK timeouts. No telemetry, Offline, and Refreshing remain separate. Thresholds currently live in the GTK presentation layer, are intentionally conservative initial defaults, and should later be configurable and evaluated on the publishing node. This view is client-local and does not initiate any additional network requests.\n\nThe native Resource Explorer groups node-owned, read-only `performance-stat`
inventory entries beneath **Statistics / Performance** on **each site**, including
linked sites. Rows use existing authenticated inventory and owner-authoritative
catalog replication; no separate RPC, filesystem browsing privilege, or
remote control operation is introduced.

Current entries include process resident/peak memory and thread count (Linux
`/proc/self/status`), open file descriptors and PTY-associated descriptors
(Linux `/proc/self/fd`), catalog snapshot/ack statistics, descriptor
validation time and results, and shared event callback / deferred queue timing.
PTY-associated descriptors count file descriptors, not unique PTY pairs.
Device paths and process credentials are never disclosed. Process metrics
are observations from the publishing server, not measurements of the
client or an intermediate relay.

With `features.profiling=true`, catalog rows identify the most recently
completed five-minute sample and its timestamp. Event callback timing rows
currently show cumulative process counters and say so explicitly. With
profiling disabled, timing rows explain how to enable collection; basic
process metrics remain available. As with all replicated catalog data,
remote statistics may be stale after a partition or reconnection. Refresh
Explorer to retrieve the last available snapshot; this does not trigger
a profiling dump or reset counters.

## Native Resource Explorer

Open **Resource Explorer** from the GTK Status tab or press **Alt-O** from any main
window tab. `/object`, `/rig` and `/gps` also open the same manager. All known
server connections appear in one window, with the full resource inventory:

```text
server profile
   site
      station GPS / serial / media / chat rooms
      rig
         rig rooms and CAT service
         VFO
            VFO media
         rig media / GPS / serial
Local serial attachments
   local endpoint → service and server
```

The tree shows all available resources, including inactive ones. **Resource
status** shows availability or the last server inventory state; **Your
connection / subscription** shows joined rooms, media subscriptions, object
update subscription state and matching serial attachments. Tree lines and
indentation separate each layer. Expand/Collapse all, incremental name search
and a lower details pane support navigating larger inventories. UUID objects
also show cached readable properties in that pane.

Select a row to use its contextual actions:

- **Join room / Leave room** follows actual membership in that row's room.
- **Subscribe / Unsubscribe media** targets the selected stream, including GPS
  output. Join its required room first; refresh discovery if it is not known yet.
- **Subscribe / Stop server object updates** controls the existing server-wide
  object/property subscription, even when invoked on a rig or VFO. It does not
  change media subscriptions or room membership.
- **Connect serial** on a CAT service or permitted serial export asks for a
  local endpoint name. An empty device path creates a PTY; physical server
  exports always use a local PTY. **Disconnect serial** releases the matching
  endpoint. Server-local GPS receiver ports remain server-managed.
- **Connect / Disconnect server** operates on the selected row's owning profile.

The Local serial attachments branch remains usable after a server disconnects
or its inventory no longer advertises a resource. It lists each endpoint's
path, service, owning server where pinned, and connected/waiting/failed state.
This lets users release attachments independently of server discovery.

Personal state updates twice per second while the window is open, using common
client caches; the timer does not poll the server. **Refresh all** requests fresh
inventories and media metadata. Normal reconnects reload the corresponding
branch. Successful resource actions also refresh that branch once. The manager retains inventories while offline and marks those resources
as server-offline. Explicit refresh preserves the selected resource where it
still exists. Discovery does not join rooms or create serial endpoints.

Actions retain the row's server across main-window tab/server changes. Explicit
native host serial tunnels and CAT attachments bind to their originating
profile, including across reconnects. Legacy CAT endpoints created before a
server is available retain their active-server behavior. All room, media and
serial permission/ownership checks stay in their existing common/protocol paths.
No new wire operation is introduced.

This is GTK presentation. TUI and browser discovery commands retain their text
trees, and `/object <symbol|uuid>` retains cached-property output. Browsers do
not create native PTYs.

## Rig symbols and configuration identities

A configured backend such as `[rig:hf-east]` or bridge rig alias `hf-east` keeps
that identity for settings and UUID persistence. Its public object alias is
`rigN`, using the same index as media frames. Commands use `rigN.A`,
`rigN.cat`, `rigN.gps.rx` and `#site-rigN`; snapshots, inventories and linked
catalogs all publish those same names. Descriptive object names may still appear
as explanatory labels. Do not use configuration identities as command symbols.

### IRC channel state

IRC server branches show passively cached LIST channels, joined channels and
previously observed channels. LIST user counts/topics are snapshots, not live
membership guarantees. The explorer never automatically sends LIST. Joined
rows show join wall time and duration, cached roster users, IRC operators known
from WHOIS/WHO, channel operators, voiced users and ordinary users. Channel
op/voice/ordinary categories are exclusive; IRC operator knowledge overlaps.

Lines seen count incoming channel PRIVMSG/NOTICE messages from other nicks;
lines sent count successful IRC message sends. Highlights count actual GTK
nickname highlight detections. Counters last for this client process, per server
profile/channel, and survive part/rejoin; join duration restarts on each join.
Disconnect clears current membership and LIST availability while retaining
historical counters. IRC rows support Join channel and Leave channel actions.

Resource Explorer sorts siblings naturally and without case distinctions, so
`rig2` precedes `rig10` at each hierarchy level.

Linked-server inventories include GPS inputs/outputs and coordinates, media,
CAT bindings and permission-filtered serial exports beneath their site, rig or
VFO. Service catalogs refresh on topology/media changes and every 30 seconds.
Per-client subscription and connection status remains client-owned. Physical
serial and CAT attachments require a direct connection to the owning server;
the Explorer labels these `owner-server-only` rather than attaching a same-named
resource on the current server. Existing peers must be upgraded to advertise
service metadata.

Dynamic SQLite authentication checks `PRAGMA data_version` before logins and
commands, plus once per second for idle sessions. External committed password,
permission and enabled-state edits therefore need no rehash. Failed refreshes
reject new authentication and commands rather than authorizing stale accounts.
Password changes affect subsequent authentications; existing sessions are
reconciled for permissions, disabled accounts and expiry.

All registered media types are listed, including webcams, other video and file
sources. Streams whose rig/VFO parent is absent fall back to the site, so they
remain discoverable. Exported shares appear as `share` entries; use `/files`
or the GTK **Files…** button to browse them. See [Exported file shares](file-shares.md).

The GTK explorer refreshes each connected server inventory every 30 seconds
while open, so catalogs received after the initial listing become visible
without reopening the window. **Refresh all** requests an immediate update.

Known effective coordinates accompany site and rig inventory rows as well as
GPS services, including linked sites. GTK uses standard tree indentation and
shows coordinates in Details. Subscribe on the GPS output row for live updates;
the corresponding site/rig Details also follows received GPS positions.


### Explicit inventory ownership

Inventory rows may carry `inventory.parent`, containing the UUID of their
owning site, rig, or VFO. Explorer uses it to place independently collected
shared folders, GPS services, serial ports and relayed resources under the
correct parent even if inventory contributors emit entries out of order.
`inventory.depth` remains available for older senders without explicit
parent UUIDs. SITE detail panels display the advertised
`inventory.description` and `inventory.location` fields from the server's
`station.description` and `station.location` configuration.

## Mesh-ready neighbor health (initial implementation)

Each rrserver observes its **directly configured peers** rather than inferring link quality from a relayed topology. Its existing inventory/catalog publishes read-only `Neighbor <site>` performance entries with direct-link state, observed ping/pong RTT and recent unexpected disconnect/auth rejection counts. GTK Cluster Health recognizes server-published Warning and Critical states and highlights affected health entries. These are **observer reports**, not a mesh-wide consensus; a disconnected peer can still be healthy from another neighbor's perspective.

A local SQLite `cluster_health` table stores append-only observations with separate `reporter`, `subject`, `category`, `severity`, `value`, `reason`, and observation time. Current probes cover established WebSocket ping/pong RTT (warning 250 ms, critical 1 s), unexpected disconnects (critical at 3 per five-minute window), and configured-peer handshake authentication failures (critical at 5). Administrative closures and shutdowns are not counted as flaps. Full distributed ingestion, deduplication, retention/pruning, and multi-observer reconciliation remain to be implemented before considering this comprehensive mesh health tracking. No credential contents are persisted in these records.
