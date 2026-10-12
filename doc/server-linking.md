# Server linking: tree-first implementation

Server linking starts as an IRC-style tree, with multiple direct peers and
multiple hops. A server has one authoritative owner UUID, and each remote
server has exactly one next hop. A future mesh protocol requires a separately
negotiated routing capability; redundant links are not admitted as standby
routes in the initial tree protocol.

## Implemented foundation

`rrserver/link.tree.c` owns topology admission, incremental introductions,
next-hop lookup, enumeration, and source-owned subtree withdrawal. It accepts
canonical lowercase UUIDs and a bounded server count (maximum 256, including
local). It has no socket, user-interface or configuration dependencies.

A complete incoming component is checked before any state changes:

- The peer is its single root; all remaining parent chains reach that root.
- Every server UUID is unique and absent from the existing local component.
- Missing parents, cycles, additional roots, self-links and capacity overflow
  reject the whole component without publishing partial routes.
- Incremental introductions extend only the authenticated source peer's branch.
- Only the owning peer may withdraw a remote subtree. Link loss withdraws that
  peer and every descendant, preserving other peer branches.

The returned next-hop UUID must be resolved through the live peer-session
registry. Socket pointers are not stored in the topology. Local destinations
must be handled locally; a NULL route also means an unknown destination.

The component test is discovered by the existing rrserver suite in
`tests/run-tests.sh`. It exercises multiple peers, multi-hop routing, rejected
snapshots, source ownership, capacity limits, and branch removal.

## Live WS/WSS link sessions

`rrserver/link.c` provides `/link` on the existing client HTTP/HTTPS listeners
(and an optional dedicated listener), plus outbound
connectors using `rustyrig-link.v1`. `librrprotocol/link.wire.c` validates a
separate, bounded flat JSON wire format. Server links never enter the ordinary client socket roster. Owner-side virtual
sessions use the existing authenticated command dispatcher for application requests.

Implemented messages are `link.hello`, `link.snapshot`, `link.ready`,
`link.introduce`, `link.withdraw`, and `link.application`. Application kinds are
`catalog`, `catalog-remove`, `users`, `request`, `reply`, `state`, `subscribe`,
`unsubscribe`, and `close`. Binary media is not accepted.

Each server may configure one peer with `role=parent` and multiple peers with
`role=child`. The role describes the **remote** server, independently of which
end initiates the socket. A server without a parent is a tree root. A server
with a parent accepts children only after it has a committed route to its root;
losing its parent withdraws that route and closes child links. This initial
rooted hierarchy prevents circular parent configurations from becoming active.
Merging independent trees and automatic parent failover are not supported yet.

Snapshots are rooted at their sender. The complete snapshot is checked before
admission, and local admissions are serialized so pending branches cannot leak
into another peer's snapshot. An intervening topology generation change aborts
admission and causes a retry. Introductions and withdrawals propagate to other
established peers. Queue, message, peer, handshake and topology sizes are bounded.
WebSocket heartbeat failure closes a link and permits autoconnect to retry.

An already established connection wins over later attempts. Simultaneous pending
connections use the initiating server's UUID as a deterministic tie-break so
both ends retain the same socket. The topology is rebuilt on each reconnect;
no pending application commands are replayed.

### Peer configuration

Use `[peer]` for one peer, or `[peer:name]` for multiple peer blocks. The alias
names the local configuration entry and need not match the remote site.

| Key | Meaning |
|---|---|
| `enabled` | Explicitly set `true` to enable; `false` ignores the entire block |
| `autoconnect` | With a URL, initiate/retry when no active or pending link exists |
| `server.url` | `wss://host:port/link`; empty means incoming-only |
| `username` / `password` | Shared per-link credentials; use the same pair at both ends |
| `site.name` | Required expected remote `station.name`, matched exactly |
| `node.uuid` | Optional expected persistent remote server UUID |
| `role` | Remote role, `parent` (default) or `child` |

These are link credentials, separate from ordinary user accounts. Authentication
is mutual and also checks the expected site and optional UUID pin. Keep the
configuration containing the password private. Peer settings are read at startup;
restart after changing them. `autoconnect=false` still permits authenticated
incoming connections when `enabled=true`.

By default, `links.shared-listener=true` accepts `/link` on the existing client
listeners: normally WS on port 8420 and WSS on port 4420. `/ws` and ordinary HTTP
routes retain their client behavior. The `/link` upgrade requires the separate
`rustyrig-link.v1` subprotocol and mutual peer authentication. Its socket is
transferred out of the ordinary client roster during the upgrade handoff.

For shared WSS, enable `net.http.tls-enabled` and configure `net.http.tls-cert`
and `net.http.tls-key`. Outgoing WSS uses `links.tls-ca`, defaulting to the Debian
system CA bundle, with hostname verification. No `links.listen` is needed.

A separate listener remains optional: set `links.listen=https://0.0.0.0:4421`
with `links.tls-cert` and `links.tls-key`, or `http://0.0.0.0:8421` for explicit
plaintext operation. Set `links.shared-listener=false` to restrict incoming peers
to that dedicated listener. Dedicated and shared listeners may coexist.

`links.allow-insecure=false` is the default. Explicit local tests may use
`links.allow-insecure=true` with an `http://` listener and `ws://` peer URL.
Normal deployments use WSS. Defaults are 128 servers (maximum 256), a five-second
retry delay and a ten-second handshake timeout, controlled by
`links.max-servers`, `links.retry-seconds`, and `links.handshake-seconds`.

Example configuration fragments for two machines:
[primary](examples/link-primary.cfg) and [secondary](examples/link-secondary.cfg).
Replace hostnames, certificate paths and the shared password, and merge these
sections into each server's existing configuration. Do not replace the existing
rig, database, media or client-listener configuration.

Both examples enable autoconnect; either machine can win the initial connection.
For incoming-only operation, remove `server.url` from that side. Startup logs
show `Server link node <UUID> (<site>)`; a committed link logs
`Linked site <site> (<UUID>), root <UUID>, servers <count>`.
After a peer restart, expect `Unlinked site ...` followed by a new `Linked site ...`.
Use the logged UUID for an optional peer `node.uuid` pin.

The component-owned live test launches three real rrservers with disposable
certificates/databases and local libraries. It exercises named/plain sections,
mutual credentials, certificate trust rejection, protocol separation,
simultaneous dials, an incoming-only peer, multi-hop introduction, branch
withdrawal and automatic recovery after restarting the middle server.

## Routing and authority

Route envelopes need origin server/session, destination owner, request ID,
operation and a bounded payload. Validate that the source identity belongs to
the incoming peer's branch. Send responses along the origin route, retaining
request correlation and deadlines. Bound pending requests and all link queues.

Rooms, objects, rigs and VFOs retain owning server UUIDs. Wire stream IDs remain
connection-local and must be translated during media relay. Inventory changes,
room membership and chat precede remote control and media in implementation.
Clients keep per-connection LIST caches until explicit remote inventory and
routing are available; station-name prefixes do not establish ownership.

### Application relay

`rrserver/relay.c` owns catalogs, owner-side virtual sessions, routing and
subscription aggregation. Protocol libraries expose synchronous events for
routing, sending, publication and roster augmentation; they do not implement
federation policy or UI behavior.

Every link advertises available sites, rooms, rigs and VFO descriptors, retaining
the originating server UUID and object parents. Intermediate nodes validate
branch ownership, cache the catalog and forward it. LIST includes all available
rooms in the linked tree. `talk.remote-rooms` identifies remote names and
`talk.servers` supplies the server list; native and browser displays mark these
with `↗`, while completion retains plain names. Room names must currently be
unique across owners; local rooms take precedence on collision. Qualified
owner/room syntax remains future work.

`/users` reports public callsigns, owning sites and connected-session counts
across the tree. It never advertises passwords, network addresses or account
privileges. Responses return to the issuing window through ordinary notices.

Remote JOIN/PART, public chat, topics, non-PTT CAT controls and property writes
follow the unique route to the owner. Requests carry an issuing-session UUID
and a short deadline; expired requests are rejected. The owner requires an
enabled matching local account, checks password-expiry/change requirements,
and applies that account's privileges through the existing command handlers.
The originating server authenticates the callsign assertion; peer credentials
therefore establish identity trust, but do not import privileges. A reserved
`admin` cross-server policy remains to be defined.

Joining creates an owner-side virtual room member and obtains room metadata,
roster and the room's rig/VFO property schema and state. Replies and errors are
private to the issuing session. Owner publications, including the issuer's
resulting state, fan out only through interested branches. Multiple subscribers
share one upstream subscription; the final departure removes it. RX-only rooms
receive only their mapped VFO state. Catalog discovery alone does not subscribe
to rig state. Owner object/property messages are adapted into the receiving
connection's snapshot epoch and sequence, retaining object UUIDs and property
versions, so native and browser caches remain consistent.

Link loss withdraws the branch's catalog, online-user directory, virtual
sessions and subscriptions. Still-connected clients retain desired remote room
membership. When discovery returns, fresh JOINs recheck owner authorization and
obtain current state. Controls are never replayed. A PART can cancel a desired
room while its owner is unavailable.

Bounds include 256 topology nodes, 2,048 catalog entries, 128 originating
sessions and 128 owner-side virtual sessions, 512 subscription/desired-room
records, 24,000-byte application payloads and the existing bounded send queues.
The three-server live test covers multi-hop owner execution, account refusal,
owner-side privilege enforcement, issuer-visible state, subscription aggregation,
final unsubscribe, RX-only filtering and fresh authorization after reconnect.

Remote PTT commands remain disabled until deadline, cancellation and session-loss
release semantics are defined. Media relay and direct media, private-message
routing, cross-server moderation and additional inventory service types remain
future work. WebTransport remains a separate transport project.

Native GTK and browser transcripts make `#room` references clickable in chat,
notices, topics and LIST output. Activation selects an existing room tab or sends
JOIN through the containing tab's connection, including remotely discovered
rooms. It preserves input drafts; transcript text remains selectable.

## Transport foundation and WebTransport follow-up

Separate decoded protocol messages from Mongoose/WebSocket callbacks before
adding another transport. A bounded transport interface needs reliable control
send, media send, close, queue/backpressure observations, and capabilities.
Existing WS operation remains an adapter; existing strict wire/schema validation
must stay shared. No alternate transport bypasses authentication or permissions.

For a subsequent HTTP/3 WebTransport adapter, define length framing on reliable
streams, reliable codec setup delivery, negotiated datagram size limits, media
packetization, jitter/reordering/loss handling and WS fallback. RTT adaptation
must incorporate delivery delay and queue/loss observations. Dependency choice,
TLS/UDP deployment and browser/native adapters remain separate work items.
