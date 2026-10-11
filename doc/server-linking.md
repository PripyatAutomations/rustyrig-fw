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

This foundation is compiled into rrserver, but no server-link listener,
outbound connector, remote resources or relay is enabled yet.

## Next implementation: authenticated WSS link sessions

Wire semantics belong in librrprotocol; peer configuration, trust, topology and
routing policy belong in rrserver. Use a distinct `rustyrig-link.v1` subprotocol
and endpoint, rather than giving server peers ordinary client identities.
Server linking is not a browser protocol and needs no browser implementation.

The handshake needs peer identity, protocol/capability negotiation, configured
peer authentication, and a bounded complete component snapshot. Identity must
be the existing stable server/registry UUID, not the mutable station name.
Application authentication and TLS certificate validation both remain required.

Admission must serialize topology changes through an inventory/commit exchange.
A snapshot alone is insufficient: two concurrent joins can each inspect stale,
disjoint components and jointly create a loop. A component generation or locked
admission exchange must detect intervening topology changes, abort and retry;
loop-free admission must be proven with simultaneous-link integration tests
before links become routable. Duplicate simultaneous connections need a
consistent UUID-based tie-break so both endpoints retain the same session.

Once committed, announce introductions to other peers except the source.
Reject conflicting introductions and tear down/resynchronize the offending
link; do not silently retain a redundant route. Disconnects propagate subtree
withdrawal. Initial burst completion and restart epochs prevent stale presence
from surviving a reconnect. Exported snapshots must be rooted at the sender,
with parent edges reoriented as needed, and exclude the recipient's branch.

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
