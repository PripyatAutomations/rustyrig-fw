## Bootstrap identities and future direct mesh links (design; not enabled)

The packaged configuration is `/etc/rustyrig/rrserver.cfg`, with included
`/etc/rustyrig/rrserver.*.cfg` files (source: `config/`). The deprecated
`doc/*.cfg.example` files have been removed. Client settings live in
`/etc/rustyrig/rrclient.cfg`, and standalone callsign lookup settings in
`/etc/rustyrig/callsign-lookup.cfg`.

A server belongs to **exactly one** configured `domain`. Only explicit
`[peer]` / `[peer:name]` entries bootstrap initial links. The existing
tree transport remains active while mesh discovery and authenticated direct
links are built; connecting every known peer immediately would currently
trigger overlapping-component rejection.

### First startup: durable server identity

Each server should generate a **persistent Ed25519 signing keypair** if and
only if no identity exists. Store the private key on that server alone, in
a dedicated root/server-owned, mode-0600 file under /var/lib/rustyrig
(with a mode-0700 parent directory), and never export it in catalog
responses, logs or wire frames. Create it atomically with exclusive creation,
fsync, and safe ownership checks; do not silently replace a corrupt or
inaccessible existing key. A temporary file must not become an identity
until securely persisted. Restart must reuse the same private key.

Publish the canonical server UUID, domain, public key, fingerprint, public
link endpoint(s), and identity generation metadata as a **signed**
server-owned catalog record. Validate the signature and owner UUID of
updates; never accept keys attached to another server's catalog entry.
Key rotation must be explicit, signed by the previous trusted key or
approved out-of-band. Unexpected key replacement is an identity conflict,
not routine rediscovery.

### Signed catalog enforcement

`cluster.force-signed-catalog=true` (the default in `rrserver/defconfig.c`)
rejects unsigned incoming descriptor updates and removals, and prevents
unsigned descriptors from being published through catalog snapshots or deltas.
The package config `/etc/rustyrig/rrserver.cfg` shows the commented
`cluster.force-signed-catalog=false` override for temporary compatibility.
Unsigned entries always generate warnings, even with enforcement disabled.
Invalid signatures or mismatched owner/domain bindings remain rejected.
Servers must be upgraded together to participate in a strictly signed cluster.

Signed records carry `identity.owner`, `identity.domain`,
`identity.public-key`, `identity.scheme`, and `identity.signature`.
Local Ed25519 signing keys persist in `/var/lib/rustyrig/identity/`.
Verified public keys are first-seen pinned in the master SQLite database.
This is initial **TOFU** enrollment: independent verification or approval of
bootstrap peers remains essential, since a compromised first relay could
supply substitute identities before any pins are established. Catalog signing
does not yet implement authenticated direct WSS mesh admission.

### Catalog as the domain trust authority (private CA without a CA certificate)

The authenticated domain catalog is the source of expected server public keys,
not a conventional X.509 CA. The `server UUID + domain + Ed25519 public key`
binding is **durably pinned** after initial trust establishment. The catalog
may distribute signed key bindings, but a previously trusted catalog relay must
not silently replace an existing pin. Conflicting identities, rotations, or
simultaneous duplicate UUIDs must fail closed and require an independently
authorized rotation or administrator confirmation. Catalog records must be
signed by their owning servers, and the catalog transport must be authenticated
through an already pinned bootstrap peer (or independently verified by an
administrator on first contact). Trust on first use is a documented policy
choice, not protection from a malicious first bootstrap server.

Direct peer links use `wss://`, but do **not** require a public CA:
the outbound connector validates the presented TLS certificate or authenticated
TLS public key against the public identity/fingerprint pinned from the catalog,
and checks the intended destination server UUID and domain. Until that pin is
available and trusted the connection is not permitted to carry mesh traffic.
A self-signed certificate is acceptable only when the pinned identity is
cryptographically bound to its TLS key: use the server's certified TLS public
key as the catalog pin, or have the persistent Ed25519 server signing key
explicitly sign a bounded TLS-certificate/SPKI binding including server UUID,
domain, validity period and key purpose. Do not assume an Ed25519 catalog key
automatically authenticates an unrelated TLS certificate. Do not simply disable
certificate verification and call the link authenticated; use a TLS backend
which supports explicit certificate/public-key pin validation.

The bootstrap peer's own WSS identity must be pinned/configured independently
to avoid circular trust. After verified catalog import, trust and route
discovery may proceed; the catalog is not itself a credential granting
unrestricted link admission, and domain membership/authorization checks still
apply.

### Authenticated end-to-end ephemeral key exchange

A relaying bootstrap peer can convey the two endpoints' signed ephemeral
X25519 public keys, but must not learn their shared secret. Each endpoint
signs a canonical transcript containing both permanent server IDs, domain,
both ephemeral public keys, role, nonce, protocol version, intended target,
and short expiry. Verify the peer's Ed25519 identity against a previously
pinned/approved public key; a catalog alone does not authenticate its own
publisher against a malicious relay. Derive a short-lived, direction-bound
key using HKDF-SHA256 from the X25519 secret and full transcript, then
authenticate the **direct** connection with that result. Never reuse a
bootstrap link password, ephemeral private key, nonce or derived traffic key.

Reject replayed, expired, mismatched-domain, swapped-endpoint, untrusted,
or unsolicited exchanges; cap in-flight handshakes and bound wire sizes.
An established server-to-server connection can relay *handshake envelopes*
but cannot become a new transit route merely by forwarding a key exchange.
The receiving server decides whether to admit that link. TLS certificate
verification and normal destination ACLs still apply; cryptography is not
a substitute for link admission policy.

### Discovery and link topology

Bootstrap links first load and validate the domain catalog. Then each
server considers reachable peers with known owner identity and advertised
link endpoints. Reaching the same server through two paths does not create
two servers or duplicate its rigs. The new graph router should support
multiple direct edges, but **current tree-based admission/forwarding must
not be bypassed** until it is replaced by loop-safe link-state forwarding.

This section describes the planned mesh exchange and identity lifecycle.
No new cryptographic identity generation, catalog signing, ephemeral
exchange or multipath forwarding is yet implemented.

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

Peers are symmetric: any server can connect to any configured peer, and either
endpoint may initiate the connection. No parent/child role is required. Each
server roots its routing table at itself. Admission exchanges complete topology
snapshots and rejects overlapping components, preventing cycles. Disconnected
components can merge; losing a link withdraws only that branch and preserves
other links. Rebuild both endpoints together: the hello message no longer carries
a global root or a peer role. Legacy `role` settings are ignored and can be removed.

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

`[general] station.name` names this server; `[peer] site.name` names the other
server. For example, nycvny37 has `station.name=nycvny37` and a peer with
`site.name=rplywv00`; rplywv00 has the opposite pair. Startup rejects a peer
whose expected site equals the local station name. Invalid peer configuration
logs identify the rejected section/key and reason without displaying passwords.

| Key | Meaning |
|---|---|
| `enabled` | Explicitly set `true` to enable; `false` ignores the entire block |
| `autoconnect` | With a URL, initiate/retry when no active or pending link exists |
| `server.url` | `wss://host:port/link`; empty means incoming-only |
| `password` | Shared link password; use the same password at both ends |
| `site.name` | Required expected remote `station.name`, matched exactly |
| `node.uuid` | Optional expected persistent remote server UUID |
| `clock-tolerance-seconds` | Per-peer clock compensation bound, 0–86400 seconds (24 hours); default 0 (disabled) |

No user account or username is required for a peer. Authentication is mutual:
each endpoint verifies the shared password, expected remote site and optional
UUID pin. Peer and listener ACL identity selectors use the remote site name,
without account groups (for example, `allow=nycvny37@10.0.0.0/8`). Keep the
configuration containing the password private. Peer settings are read at startup;
restart after changing them. Legacy peer `username` keys are ignored and omitted
when saving configuration. Rebuild both servers together: the hello message
no longer carries a username. Usernames in relayed application requests still
identify the user for owner-side account authorization. `autoconnect=false` still
permits authenticated incoming connections when `enabled=true`.

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

To **accept incoming links only on a firewall-controlled port**, configure
these in the included `rrserver.site-local.cfg` under `[general]`:

```ini
links.shared-listener=false
links.listen=https://0.0.0.0:4421
links.tls-cert=/etc/rustyrig/link-cert.pem
links.tls-key=/etc/rustyrig/link-key.pem
```

Configure the `listener:link.acl` policy and restrict inbound TCP/4421 to
your trusted peers at the firewall. Peers use `wss://hostname:4421/link`.
Ordinary `/ws` and web clients retain their existing listeners. If
`links.shared-listener=false` is set without `links.listen`, the server
has no incoming link listener (outgoing links may still work).

`links.allow-insecure=false` is the default. Explicit local tests may use
`links.allow-insecure=true` with an `http://` listener and `ws://` peer URL.
Normal deployments use WSS. Defaults are 128 servers (maximum 256), a 60-second
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
`Linked site <site> (<UUID>), servers <count>`.
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

`rrserver/relay.c` owns routing and lifecycle; `relay.catalog.c` owns discovery,
`relay.sessions.c` owns virtual sessions and subscription aggregation, and
`relay.media.c` owns stream translation/reassembly. Shared state is declared
only in `relay.internal.h` and has one definition in its owning domain. Protocol libraries expose synchronous events for
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

Remote JOIN/PART, public chat, topics, CAT controls and property writes
follow the unique route to the owner. Requests carry an issuing-session UUID
and a short deadline; expired requests are rejected. An authenticated
`clock.unix` handshake sample estimates clock offset without changing the
system clock. Setting `clock-tolerance-seconds=120` in the receiver's matching
`[peer]` section enables compensation for directly connected request origins
only when the offset is within 120 seconds. The validity window remains
approximately 40 seconds after correcting the timestamp; this does not extend
the replay lifetime by 120 seconds. Forwarded requests from downstream nodes
are not compensated with the intermediate hub's clock estimate, since that
is not the originator's clock. NTP remains recommended; Unix timestamps do not depend on the configured timezone. The owner requires an
enabled matching local account, checks password-expiry/change requirements,
and applies that account's privileges through the existing command handlers.
The originating server authenticates the callsign assertion; peer credentials
therefore establish identity trust, but do not import privileges. A reserved
`admin` cross-server policy remains to be defined.

Joining creates an owner-side virtual room member and obtains room metadata,
roster and the room's rig/VFO property schema, state and private media discovery. Replies and errors are
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
The four-server live test covers multi-hop owner execution, account refusal,
owner-side privilege enforcement, issuer-visible state, subscription aggregation,
final unsubscribe, RX-only filtering and fresh authorization after reconnect.

Remote PTT is allowed only while the owning server has a live, authorized direct
TX media attachment for that session and room. It uses the owner's existing
PTT interlocks, account privileges and accounting. Owner-side virtual sessions
participate in the talker lookup. Loss of authenticated UDP keepalives releases
PTT after three seconds; room departure, unsubscribe and link/session loss revoke
attachments and release PTT. A backend release failure retains the virtual
session for retry rather than freeing accounting references. Reconnection never
replays PTT or other controls.

Private-message routing, cross-server moderation and additional inventory service
types remain future work. WebTransport remains a separate transport project.
See [Direct media](media-direct.md) for RTP configuration and media fallback.

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

### Stale local room metadata

An authenticated owner's node or rig descriptor establishes authority for its
station lobby or rig room. When that room was saved locally under an earlier
configuration, discovery removes the obsolete local database row and VFO
bindings, preserves chat history, and replaces the catalog claim with the remote
owner. Connected local members are rejoined through the owner and its account
checks. The active local station/rig rooms remain protected. Ordinary chat-room
name collisions do not establish authority and do not trigger automatic deletion.

Peer `retry-seconds` overrides the global maximum reconnect holdoff (default 60 seconds). After each disconnect, a fresh random delay between 1 second and that maximum is chosen for the next attempt, reducing simultaneous reconnect storms. `links.startup-delay-seconds` remains the separate initial startup grace period. Admins/owners can
manage stored peers with `/link list|open|close`; see
[Connection recovery and administration](connection-admin.md).

## Offline room tabs

When an owning site becomes unreachable, the connected server sends
`talk.room-state` with the room and `talk.connected=false`. Clients retain the
room tab, replace the displayed roster with OFFLINE and disable rig controls.
GTK, TUI and browser leave ordinary chat input in its buffer without sending
or recording it in history. Slash commands remain available, including `/part`
and window closing. Pending relay subscriptions are restored when the owner
returns; the resulting self JOIN clears the offline state and refreshes the
roster and controls. Preserved drafts are not sent automatically.

### Quiet relay roster polling

Local user-directory snapshots rebuild after authenticated room joins, session
closes, source-role hellos and account/config reload/edit events. Notifications
coalesce until the next normal server poll. A five-second monotonic safety
reconciliation covers missed/future state changes; an explicit `/users` query
or a newly ready peer requests a fresh snapshot immediately. Only changed user
names/session counts are published to peers. Allocation failure preserves the
previous snapshot and bounds retries to one second unless another change arrives.

This throttle covers roster reconstruction only. Media fragment expiry, request
retries, authentication revocation and failed PTT key-up retries retain their
existing polling cadence. High counts of per-frame media events can therefore
remain even though quiet polls no longer allocate/rebuild user dictionaries.


## Public site and rig metadata

Set these in `config/rrserver.site-local.cfg`, which is included by
`config/rrserver.cfg`:

```ini
[general]
station.name=example-site
station.description=Community amateur radio station
station.location=Charleston, West Virginia, United States
```

The default description is `A misconfigured server` and the default
location is `Unknown City`; replace both on deployed servers. For fixed
sites use `City, State, Country`. This public identity is included in
the server's inventory and the site node descriptor distributed via the
server-link catalog. Clients display it in Explorer; server WHOIS replies
also carry the responding server's location and description.

Rig descriptors may override the site location with
`[rig:ALIAS] location=City, State, Country`. Configure
`mobile=true` under the same rig section for mobile radios: their
advertised location becomes exactly `MOBILE`. Do not substitute the
site's fixed coordinates for a moving rig's location.

### Mobile rig examples

The `mobile` setting belongs in a named rig's section, for example in
`config/rrserver.rigs.cfg`:

```ini
[rig:rig1]
;mobile=true
;location=Pittsburgh, Pennsylvania, United States
```

Uncomment `mobile=true` for a moving or portable radio. Its advertised
location becomes exactly `MOBILE`, even when its hosting server has a
fixed city/state/country location. Leave it off for a fixed radio; an
optional `location` overrides the server's `station.location`.


## Per-peer topology restrictions

Each `[peer:name]` may limit which servers are accepted *behind that direct
peer*:

```ini
[peer:remote]
;leaf-only=true
;allowed-sites=branch-a branch-b branch-c
;max-depth=2
```

`leaf-only=true` requires the remote peer to advertise no descendants.
`allowed-sites` is a space- or comma-separated allowlist of site names for
**descendants** (the direct peer is already identified by `site.name`). An
empty list accepts any descendant name. If `max-depth` is not specified on a peer, it inherits `links.max-depth` (default 5). Explicit `max-depth=0` is unlimited; 1
permits direct children, 2 permits grandchildren, etc. `leaf-only=true` overrides any depth limit and rejects all descendants. These controls are
checked before attaching an initial subtree and for subsequent introductions.
If the incoming topology violates a restriction, the link is closed and the
peer is blocked from automatic reconnection. An administrator can retry with
`/link open <peer>`, which clears the block; `/link list` shows it.
These limits are local admission policy, not permissions delegated to peers.
Names alone are not authentication credentials; pin direct peer UUIDs with
`node.uuid`, and use passwords, TLS and ACLs as usual.


## Startup connection grace period

`links.startup-delay-seconds=5` (default) delays **automatic outbound** peer
connections until five seconds after link initialization. The setting accepts
0–300 seconds; 0 restores immediate automatic connection attempts. Incoming
connections and manual administrative `/link open` are not delayed by this
setting. Existing handshake and reconnect timers still apply afterward.

This reduces simultaneous-start races, but does not replace investigating
repeated topology-admission failures or peers disabled by policy. Check
`/link list` and the `link` log category to distinguish automatic retry
from a topology-policy block.


Automatic reconnects now stagger each peer's retry by up to five seconds
using its configured site name. This reduces repeated simultaneous link
collisions without introducing random state or disabling reconnects.


## Remote administration grants and hop policy

A grant subject `user@*` means **that username authenticated through any
linked origin site**, rather than a command sent to all servers. For example,
on the destination server, as an owner:

```text
/admin resource-grant joe@* site * remote-admin allow remote
/admin resource-grant joe@* site * admin allow remote
```

This applies only on the server storing the grant. It matches `joe` arriving
from a linked station, not arbitrary users and not unauthenticated claims.
Individual `joe@site-name` grants still work. Explicit denies take precedence
over wildcard allows in the resource resolver.

The `[general]` setting `remote-admin.next-hop-only=true` in
`config/rrserver.peers.cfg` is the default. It allows remote administration
only for a client whose **origin server is a directly linked peer** of the
destination. Set it to `false` on the **destination** to permit administrative
requests from identities authenticated farther away in the tree. Intermediate
servers forward normally and do not evaluate their own administration policy
for transit requests. Destination-side `remote-admin` and operation grants
are always required.

Use `/admin @NODE_UUID users` to target one linked server. `@*` is
**not** an administration-command broadcast target; it belongs only in
privilege subjects such as `joe@*`.

## Origin provenance and future mesh routing

A linked application envelope carries the claimed originating **node UUID**.
At every incoming authenticated link, `rrserver/link.c` requires the
link peer to be the current tree route toward that node:
`rrserver_links_route(origin) == authenticated_peer_node`. A mismatch
closes the link before the application or remote-admin handler receives it.
Transit nodes route toward the destination; they do **not** use their own
`remote-admin.next-hop-only` setting to block a packet addressed elsewhere.

This is **topology-bound origin validation**, not end-to-end cryptographic
proof that a particular human logged in. Each trusted originating server
vouches for the username included in the envelope. A peer that controls an
entire branch could impersonate users *within that branch*, but must not be
able to claim identities from an unrelated branch. The destination evaluates
the authenticated origin UUID, its advertised site alias, grants, and its
own hop policy.

Mesh design requirement: separate (a) origin node identity, (b) authenticated
immediate ingress neighbor, and (c) routing path. Multipath routing means
the tree's single-next-hop assertion will no longer be sufficient; replace it
with verified path/adjacency or end-to-end authenticated provenance, including
loop/replay controls, rather than trusting forwarded origin strings. Preserve
the origin identity through transit independently of route changes.

Regression tests needed before v1.0: a sibling peer forging another branch's
origin must be rejected; a valid two-hop request must succeed when authorized;
a revoked `user@*` grant must stop further admin actions; the destination
must deny a distant origin when `next-hop-only=true` even if transit servers
are permissive. These require live linked-server fixtures.
