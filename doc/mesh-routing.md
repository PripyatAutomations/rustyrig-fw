# Mesh routing policy interface (experimental)

This work is on the `mesh-linking` branch. Configured peers in
`/etc/rustyrig/rrserver.peers.cfg` are bootstrap links only. They establish
initial trusted domain membership and catalog discovery. Servers then discover
reachable same-domain peers, verify catalog-distributed but durably pinned Ed25519 public identities, exchange
signed ephemeral X25519 keys end-to-end through existing links, and attempt
new direct connections. The relay cannot derive an honest exchange's secret;
a malicious relay could substitute keys unless endpoint identities were
independently pinned/approved. The current tree forwarding implementation must
not accept redundant edges until the loop-safe mesh path is operational.


`rrserver/link.mesh.[ch]` implements bounded shortest-path calculation separately from the existing `link.tree` transport. It is compiled into rrserver but **not enabled for live forwarding**.

Each `rr_mesh_t` holds an authenticated/validated topology snapshot, scoped to one local node. The routing provider receives a read-only graph and destination UUID, returning a next hop, total path cost, and hop count. The host rejects providers with an incompatible `RR_MESH_ROUTER_ABI` version and rejects decisions whose next hop is not directly adjacent. Current built-in policy is `rr_mesh_router_dijkstra`, deterministic on equal-cost paths; cost is an abstract positive link metric, not a health color.

`rr_mesh_set_router(mesh, provider, context)` selects a policy for an instance; NULL returns to Dijkstra. The interface is deliberately plain C, making it suitable for a future dynamic module adapter built on the existing module lifecycle. **Dynamic library loading, safe hot-unloading, topology dissemination, epoch/sequence tracking, replay protection, loop-free forwarding, path hysteresis, multipath, and consensus about remote observations are not implemented yet.** A plugin must not gain authority to create sessions or bypass link authentication.

Before enabling mesh forwarding, add an authenticated link-state advertisement with origin UUID, sequence, bounded lifetime, deduplication, route-epoch semantics, and validation at every hop. Stage deployments behind a feature flag so v1.0 remains on the proven tree transport. Health observations should be kept per observer/neighbor edge and fed into cost policy as independent telemetry, not averaged into a node-level red/yellow status.

## Operation mode and persistent connections

`net.mesh-linking=true` is the new default feature gate; setting
`net.mesh-linking=false` in `/etc/rustyrig/rrserver.cfg` selects the
existing manually configured tree links only. The gate is reserved for new
auto-discovery/direct-link features; they are not live yet. Tree mode remains
supported indefinitely, including in mixed-version deployments where secure
mesh capabilities have not been negotiated.

Each trusted same-domain server should maintain one authorized direct
connection to every reachable peer, subject to bounded concurrency and
connection budgets (for N fully connected nodes there are N*(N-1)/2 links).
Keep the existing 10-second WebSocket ping cadence and roughly 30-second
heartbeat deadline as initial defaults. Failure triggers bounded randomized
backoff rather than simultaneous reconnect storms. Crossed dial attempts use
the existing UUID tie-break; outgoing dial target and incoming acceptance must
be the same authenticated pinned identity. A failed direct edge must not
disconnect an otherwise healthy bootstrap/tree path.

## Warm catalog persistence (planned, not enabled)

Maintain a domain-scoped SQLite cache of remote catalog descriptors, keyed by
`(domain, owner-server-uuid, object-uuid)`, alongside owner public-key pins.
Persist source signatures, source revisions or sequence/epoch, generation IDs,
receive timestamp and last verified synchronization time in a transaction.
Use a WAL-backed database where appropriate; never reuse stale authorization
grants, session tokens, subscription state or media routing state from the
cached catalog. Server identity pins require a separate durable lifecycle and
must not expire together with transient descriptors.

On a restart, a cache no older than **5 minutes** since the last fully
acknowledged, authenticated synchronization can pre-populate **read-only
discovery** as `stale`. Never promote it to live/controllable resources until
an authenticated peer confirms owner identity, topology and current catalog
generation. Older snapshots may remain on disk for diagnostics, but must not
advertise live presence. A disconnected server should retain its cached
descriptors without exposing operational routes; reconnect fetches deltas
only when its owner sequence/generation matches, otherwise full resync.
Apply deletion/tombstones transactionally. Clock skew and missing trustworthy
timestamps must force a full sync.

Keep bounded retention and size limits, index by owner and UUID, and purge
removed/expired descriptors without deleting long-lived identity pins.
A cache is **not** a quorum or source of authority: server-owned signed
records and fresh authenticated links remain authoritative.
