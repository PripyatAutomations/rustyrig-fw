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
