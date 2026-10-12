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

The returned next-hop UUID must be resolved through the future live peer-session
registry. Socket pointers are not stored in the topology. Local destinations
must be handled locally; a NULL route also means an unknown destination.

The component test is discovered by the existing rrserver suite in
`tests/run-tests.sh`. It exercises multiple peers, multi-hop routing, rejected
snapshots, source ownership, capacity limits, and branch removal.

## Live WS/WSS link sessions

`rrserver/link.c` now provides a dedicated `/link` listener and outbound
connectors using `rustyrig-link.v1`. `librrprotocol/link.wire.c` validates a
separate, bounded flat JSON wire format. Server links never enter the ordinary
client roster or the client command dispatcher. No browser changes are needed.

Implemented messages are `link.hello`, `link.snapshot`, `link.ready`,
`link.introduce`, and `link.withdraw`. Binary media and routed application
commands are not accepted yet. This release establishes server topology only;
remote rooms, chat, rig control and media relay remain the next layer.

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

Under `[general]`, set `links.listen=https://0.0.0.0:8443`,
`links.tls-cert`, and `links.tls-key` for the listener. Set `links.tls-ca` to the
CA bundle used to validate outgoing certificates; its default is the system
Debian CA bundle. Hostname verification remains enabled. The certificate must
cover the hostname in `server.url`. The dedicated TCP listener port must be
reachable between the servers. An outgoing-only server can omit `links.listen`.

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

### Application relay contract (next implementation)

Discovery and live state are separate. Every admitted link carries the catalog
of available sites, rooms, rigs, VFOs and other objects, with their owning node
UUIDs and object-parent relationships. An intermediate server caches and forwards
that catalog; it does not become the owner. Object descriptors and capabilities
may be advertised without a subscriber. Property values, room traffic and other
live state require subscriptions. Media transport remains deferred, including
both relayed and direct-media modes.

A control request follows the unique route to its owning node, rather than
being broadcast to peer links. Intermediate nodes validate the origin branch
and destination and forward the request without applying it. The owner checks
the user, room membership, permissions and backend constraints through the same
authoritative handlers used for locally connected users. A forwarded request
must not carry an implicitly trusted local admin role. Remote user identity and
owner-side account authorization must be settled before enabling execution.

Subscriptions are aggregated per object or room and per next-hop branch. The
first interested local client or downstream branch creates an upstream
subscription; the final unsubscribe removes it. Joining a room subscribes to
that room's chat and required rig/VFO state. Merely displaying the catalog does
not subscribe to every rig. A subscription is committed by the owner, and its
initial state precedes subsequent changes so joining during a VFO adjustment
cannot leave a client with an older value.

Only the owner publishes resulting state. It sends each change once to each
interested link and to interested local users. Intermediate nodes fan that
change out to their subscribed local users and downstream branches, excluding
the incoming link. This includes the command issuer; clients must reconcile
with the owner's result rather than treating a locally sent command as applied
state. State versions retain their owner identity; combining several owners on
one client connection must not mix their epochs or reset another owner's cache.
The current C and browser caches use one snapshot epoch per connection, so this
requires an explicit stream adaptation or synchronized cache/protocol change.

Request acknowledgements and errors return to the issuing session, separately
from subscriber state fanout. Private messages retain their recipient scope.
Owner-origin validation applies to state and catalog messages as well as
requests: a peer may advertise only objects owned by nodes reachable through
its branch. Bounds apply to catalog entries, subscriptions, outstanding requests
and payload sizes, in addition to the existing link send queues.

Link loss removes affected catalog entries, subscriptions and pending requests.
Reconnect first rebuilds the catalog and then obtains fresh snapshots for
subscriptions that still have local demand. It never replays queued controls.
Tests must cover an idle branch receiving discovery but no live state, two
subscribers sharing one upstream subscription, final unsubscribe, multi-hop
owner execution with issuer-visible state, unauthorized controls, and withdrawal
or reconnection while a request or snapshot is in flight.

The rig owner alone authorizes, arbitrates and executes PTT. Never replay PTT-on
on reconnection. Define cancellation/deadline and session-loss release semantics
before exposing remote TX. Do not import a peer's admin role as local authority.
Remote authentication, accounting, logs and moderation need explicit policies.

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
