# Multi-cluster identity (protocol design)

RustyRig server profiles identify individual connections. They are **not** network identities. A cluster (also called a domain) is a separately administered group of servers that share network membership and routing. Two independent clusters can contain a server or room with the same human-readable name.

## Required identities

- `cluster.id`: persistent random UUID, shared by every server in one network; MUST NOT derive it from DNS, IP, server name, a relay, or a callsign.
- `cluster.name`: human-readable label, not an authorization or routing key.
- `server.id`: persistent unique node UUID within and across clusters.
- `server.name`: display label; it is not guaranteed globally unique.
- A resource's canonical reference is `(cluster.id, server.id, resource.id)`. Human-readable room names are qualified by cluster.

There is no compatibility fallback. If a peer does not advertise a valid cluster identity, do not combine its catalog or topology with another network.

## Protocol

1. After transport establishment, the server announces its `cluster.id`, `cluster.name`, `server.id` and capabilities as part of authentication/hello. The client stores these on the **connection**, not in `ws_conn`.
2. A server-to-server link confirms that both sides agree on the cluster identity before exposing routes/resources. Cross-cluster federation, if introduced later, must be explicit and must retain the original cluster ID of every resource.
3. The client groups connected profiles by cluster ID and keeps independent network catalogs, room identities, grants, and topology snapshots for each cluster.
4. Refresh callbacks and media, file and resource operations retain the owning connection and cluster ID. Reject responses whose ownership differs from the request.
5. Disconnect invalidates work for that connection. When the last live connection **in a cluster** closes, remove that cluster's live catalog; do not erase catalogs or connections belonging to other clusters.
6. For display use `cluster.name / server.name / room`, disambiguating duplicate display names with short UUIDs. Keep stable IDs in serialized references.

## Migration order

1. Add a persistent cluster UUID and name to server configuration and durable database; independently persist a server UUID.
2. Transmit identity in handshake/auth and in inter-server links. Enforce membership before route advertisements.
3. Index client resource and receiver catalogs by cluster, then make lifecycle cleanup cluster-scoped.
4. Qualify room/resource lookups, privileges, subscriptions and saved client state with cluster identity.
5. Finally remove `ws_conn` as a global routing selector. An interactive default connection is allowed only at the command/UI boundary.

**Current limitation:** The disconnect-driven receiver catalog cleanup is global, not cluster-scoped. It must be upgraded with step 3 before simultaneous independent RustyRig clusters are supported.
