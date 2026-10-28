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

## Shared transport boundary (implementation rule)

Do **not** implement a second mesh websocket session manager or a second
catalog replication stack. Both modes use `rrserver/link.c` for the shared
`/link` listener, outbound connector, peer authentication, identity/key
verification, keepalives and health telemetry, application envelopes,
reconnection, and catalog synchronization. Callers route only through
`rrserver_links_route()` and transmit with `rrserver_links_send()`.

The differences are constrained to topology **admission** and **routing**:

- **Tree:** `link.tree.c` prevents overlapping components and uses its
  existing deterministic next-hop table; it remains supported indefinitely.
- **Mesh:** `link.mesh.c` will maintain authenticated link-state edges and
  select a direct next hop, with loop prevention and bounded advertisements.
- **Mode switch:** `net.mesh-linking=false` selects tree admission/routing,
  without altering common peer sessions, signatures, or catalog code.

The current `rrserver_links_route()` implementation still delegates to
`rr_link_tree_route()`. Switching it to a mesh route must wait until full
authenticated link-state propagation and loop prevention are implemented.
Both providers must return a neighbor that actually has an established,
authenticated session. Never accept a discovered catalog endpoint as a
forwarding next hop until its identity and link session are verified.

### Low-bandwidth node policy

The initial mesh policy can connect to every reachable same-domain peer, but
connection planning must be a separate optional policy (not embedded in
transport/session code). Later a constrained LTE node should be able to set
limits on direct peers, keepalive cadence, and uplink traffic while remaining
reachable through other authenticated nodes. A selected subset must preserve
bootstrap reachability and avoid uncontrolled reconnection storms.

## Synchronization, quorums and revision authority

The health message `2/2 connected peers bidirectionally synchronized`
means *locally observed* link health: both direct neighbors completed an
acknowledged catalog exchange in both directions. It does **not** mean two
of two cluster members, total domain membership, end-to-end convergence,
or a majority consensus quorum. An offline remote owner may be unreachable,
a relay may hold an outdated view, and a partition may form two healthy
local components.

A consensus quorum is not needed to authorize server-owned discovery:
the owning server signs its records, and a receiver validates its
identity and strictly monotonic content revision. Quorum/consensus would
be relevant only if introducing *shared mutable state* or automatic
cluster-wide decisions such as membership revocation, electing a unique
writer, or coordinated key rotation. If that becomes necessary, report
a separately named end-to-end convergence metric instead of conflating it
with direct-neighbor synchronization.

## Signed catalog revisions (initial implementation)

With SQLite enabled, local descriptor content hashes and monotonically
increasing `identity.revision` values persist in `mesh_catalog_revisions`
and are signed along with each finalized descriptor. Recollecting an
unchanged descriptor keeps the same revision, avoiding needless deltas.
Receiving servers maintain their own domain/owner/key high-water mark:
older revisions and equal-revision conflicting payloads fail closed;
identical signed retransmissions are allowed. The revision state is shared
by tree and mesh catalog code.

**Deployment note:** all servers must upgrade together with strict signing
enabled: old signed descriptors without `identity.revision` will be rejected.
Do not clear `mesh_catalog_revisions` independently from long-lived
signing keys; resetting a signing owner's counter risks remote peers
rejecting legitimate new revisions.

**Not complete:** signed removals do not yet carry a per-object tombstone
revision and high-water comparison, and cache persistence of the full
catalog remains planned. A revocation/tombstone barrier must be added before
accepting arbitrary replayed catalog upserts in a dynamic mesh; the current
revision guard alone does not prevent resurrection of an equal-revision
entry that was previously deleted.

## Client-verifiable catalog trust rooted in server TLS identity

The TLS certificate authenticates the directly connected host, not every
remote owner whose catalog is relayed through that host. Therefore client
verification requires both (a) certificate-anchored owner identity and
(b) the original owner's immutable signed catalog descriptor.

A server's long-lived Ed25519 catalog public key MUST be cryptographically
bound to its TLS certificate identity (for example an owner-signed
certificate-key binding proof, validated against a pinned certificate/SPKI
or a verified certificate chain and authorized hostname). A bare public key
inside a catalog is NOT an adequate trust anchor. Linking the TLS and
catalog keys by matching a claimed hostname without a signed proof is
also insufficient. Pinned keys must survive reconnections; rotations need
an explicitly authenticated migration or administrator approval.

The relay must forward the exact signed catalog record, without appending
client stream/request fields or modifying signed properties. Client-facing
object and inventory rows are *projections* and cannot be signature-verified
as if they were original records. Define a distinct wire message carrying
an opaque complete descriptor, source owner, and certificate binding
metadata, with request/session correlation OUTSIDE the signed payload.
Update librrprotocol wire-schema, its generated registry, C/GTK/TUI
consumers and the browser client in concert. Client caches must admit
remote records only after signature, domain, binding, revision and parent
validation; untrusted records must be labeled or excluded from controls.
Preserve high-water marks and deletion tombstones across reconnects.

The current temporary server-side filtering of identity.* from projected
object/inventory messages prevents serialization errors but DOES NOT
implement client verification. Do not simply permit identity.* in the
existing object.inventory-entry schema: those rows are transformed after
signing and would fail cryptographic validation. Existing
TLS peer authentication/CA checks must not be silently disabled for this.

## Private CA provisioning (experimental)

On a protected admin host with OpenSSL installed:

```sh
sh tools/rustyrig-private-ca.sh init-ca /secure/rustyrig-ca
sh tools/rustyrig-private-ca.sh issue /secure/rustyrig-ca /tmp/nycvny37 nycvny37.example.net 10.0.0.37
sh tools/rustyrig-private-ca.sh fingerprint /tmp/nycvny37/cert.pem
```

Keep `/secure/rustyrig-ca/ca.key.pem` offline. Copy only the issued
`cert.pem`/`key.pem` onto that host, using the existing RustyRig
certificate configuration. Copy `ca.cert.pem` into an application-managed
trust directory on clients, not the system trust store. All hostnames or IPs
clients connect to MUST be present in certificate SANs. The pin is the
base64 SHA-256 digest of the certificate's DER SubjectPublicKeyInfo (SPKI);
pin the public key, not the whole certificate.

**Security boundary:** the helper provisions a private CA and prints pins;
it does not yet modify Mongoose's TLS verification policy, implement the
client trust database, prove binding from TLS keys to Ed25519 catalog
identities, or verify remote catalogs in clients. Do not treat possession
of the generated CA file or printed pin as enforcement until transport
verification and peer identity binding have been integrated. Browser WSS
must still pass browser TLS validation.

## Decentralized trust enrollment (implementation in progress)

RustyRig does not require a shared CA for server identity. Each server can
use its own persistent, self-signed TLS certificate and its separate persistent
Ed25519 catalog-signing identity. Provision a test certificate using:

```sh
sh tools/rustyrig-selfsigned-host.sh /etc/rustyrig nycvny37.example.net 10.0.0.37
```

This tool refuses to overwrite keys. Record and independently verify its
SHA-256 SPKI fingerprint. The *public* key is enrolled, not the private key.
Unlike a certificate fingerprint, an SPKI pin remains the same across a
certificate renewal only when the TLS key is deliberately reused.

The SQLite `mesh_explicit_trust` table and
`rr_identity_trust_enroll()/rr_identity_trust_verify()` provide an initial
immutable store for (domain, server UUID, catalog Ed25519 public key, TLS SPKI
SHA-256). Enrollment must be a deliberate operator action after out-of-band
verification; first-seen signed catalog assertions and introductions are NOT
sufficient to approve a new server. A changed key is rejected by the store,
not silently replaced. The existing auto-TOFU `mesh_identity_pins` table is
a separate legacy mechanism and is NOT equivalent to explicit trust.

**Not yet enforced:** this initial store is not connected to Mongoose TLS
handshake verification or to catalog admission, and the certificate/catalog
binding proof, enrollment command/UI, client trust storage and independent
client signature verification are not yet implemented. Do not infer trust
enforcement from the presence of records in SQLite. Never use
`mg_tls_opts.ca = NULL` as a shortcut for accepting a self-signed peer.
Implement custom trust/pin verification in a transport backend that actually
exposes the peer certificate and fails connections on mismatch.

A trusted introduction is an authenticated *candidate* identity, not a
grant. Require separate approval or an explicit domain policy before granting
controls or remotely authorized access. Catalog revisions and signed
tombstones remain independent of TLS identity.

## Capability-negotiated signed link identity (experimental)

After the existing password/UUID/domain link handshake, both peers advertise
`identity.spki-binding-v1` in `link.capabilities`. Upgraded peers then send
a separate `link.identity` protocol message containing the server UUID and
domain, the raw Ed25519 catalog public key (base64), the self-signed TLS
certificate's SPKI SHA-256 (base64), and an Ed25519 signature over the
domain-separated, length-framed (domain, UUID, SPKI) tuple.

This is intentionally NOT an additional field in `link.hello`, because old
link decoders strictly reject unexpected fields. The receiver verifies the
cryptographic signature and that the UUID/domain match the established link,
logging a useful diagnostic. No trust or authorization is granted by this
self-asserted signature; an attacker can generate a self-consistent key/cert
pair. The TLS backend must still compare the advertised SPKI to the
certificate actually presented on that connection, enforce local enrollment
policy, and reject mismatches BEFORE marking the peer transport trusted.

Until that enforcement exists, existing TLS verification and the configured
shared peer password/ACL remain operative. This extension must not be used
as justification for removing certificate validation or passwords. For
compatibility, old peers continue through the existing handshake, without
this new advisory proof. Browser clients still require browser-trusted WSS.

## Mandatory TLS and decentralized peer enrollment (breaking change)

Server links require `wss://` or a dedicated `https://` listener.
`ws://`, plaintext shared link upgrades, `links.listen=http://` and
`links.allow-insecure=true` are rejected. There is no fallback to a
plaintext or legacy peer. Every configured enabled peer must have
`peer.tls-trust` (or `peer:NAME.tls-trust`) pointing to an
independently verified PEM certificate installed by the operator.

Bootstrap procedure: generate persistent host TLS certificates in rrserver;
on a separate trusted channel exchange each server's public *certificate*
and displayed SPKI fingerprint; compare fingerprint out of band; install
the verified PEM locally under a per-peer path; configure that peer's
`tls-trust` path; then connect via the DNS name on the certificate.
The private key is NEVER transferred.

Example:

```ini
[peer:nycvny37]
enabled=true
site.name=nycvny37
server.url=wss://nycvny37.example.net:4420/link
tls-trust=/etc/rustyrig/trust/nycvny37.pem
password=CONFIGURE_SEPARATELY
```

The current Mongoose transport uses each peer's enrolled certificate
as its outbound TLS CA/trust material and enforces hostname checks. This
must be validated against the installed Mongoose/TLS backend with
CA:FALSE self-signed leaf certificates before deployment; if the backend
does not accept an explicitly trusted leaf, implement direct SPKI validation
in the backend rather than weakening certificate checks.

The post-handshake signed `link.identity` remains necessary but is not
yet a direct verification of the certificate observed in TLS. Until the
transport binding and identity admission gating are complete, do not
consider the application-level signed assertion sufficient to authorize
new peers. Old peers without `identity.spki-binding-v1` are rejected.

## Issuer-local invitation architecture

Join invitations belong exclusively to the issuing server, not to the global
resource catalog. The inviter creates a random, single-use Ed25519 keypair
and records only the invitation UUID, public key, cluster domain, expiration,
and pending/used/revoked status in its local SQLite
`mesh_join_invitations`. The joining operator receives an invitation token
containing the private one-time credential, bootstrap server URL and pinned
bootstrap server identity, over an independent secure channel. The URL and
private enrollment credential need NOT appear in cluster-wide catalog data.

The issuer-local SQLite implementation provides create, revoke and an atomic
conditional pending-to-used transition that records the newly admitted node
UUID, catalog signing key, and TLS SPKI. **This does not yet implement the
join command or authorization proof.** The redemption transition must only
run after authenticated possession of the one-time private key, proof of the
new node's permanent public keys, and verified TLS server identity. Never
call the transition based on unauthenticated link fields.

Invitation redemption requires a challenge bound to the enrollment TLS
session, or a protocol with equally strong explicit transcript binding;
the existing link password is not an acceptable replacement. Admission and
catalog export remain gated on the permanent signed identity validation.
After an invitation is consumed, the joiner reconnects under normal strict
TLS/SPKI verification. Multi-server retry must be directed to the issuing
server, because only it can atomically consume its invitation.

Do not activate this incomplete enrollment path in a production cluster.

### Invitation expiration

New invitations are valid for **72 hours (3 days)** by default. Set
`links.invitation-expiry-hours=72` in the rrserver configuration to
change the default (allowed range: 1–8760 hours). The invitation creation
API accepts an explicit UNIX expiration timestamp, which overrides the
default; passing zero selects the configured duration. Expired invitations
are never redeemable, including if their status remains pending in SQLite.

### Issuing a join token (implementation status)

`rr_identity_invite_issue(db, domain, bootstrap_wss_url,
issuer_tls_spki_b64, expires_unix)` now creates an ephemeral Ed25519
invitation keypair, inserts the invitation ID and public key in the
issuer-local table, and returns a sensitive line-delimited token
(`RUSTYRIG-JOIN-1`) containing the ID, cluster domain, WSS bootstrap
URL, issuer TLS SPKI SHA-256, issuer catalog public key and base64-encoded
private invitation PEM. Passing zero for expiration selects the configured
default lifetime (72 hours).

The token must be delivered to the new node's administrator through a
confidential channel. The issuer must NOT log, persist, or broadcast the
private credential; the returned token is for one-time secure delivery.
This is an internal issuance API, not yet a CLI command or an operational
join protocol. The TLS peer certificate must be checked against the issuer
SPKI pin in the token before any enrollment messages are accepted. A
challenge/response binding and permanent node-key admission still need to
be connected before redemption or catalog exchange can be enabled.

### Invitation proof verifier (in progress)

The issuer now has `rr_identity_invite_verify_proof`, which checks an
Ed25519 signature against the invitation public key in its own SQLite table,
rejecting unknown, expired, revoked or already-consumed invitations. This
function **does not** establish an authenticated TLS channel, manufacture a
challenge, consume the invitation or admit a server. Its caller is responsible
for a fresh one-time challenge, TLS exporter/channel binding, and a canonical
transcript containing domain, invitation ID and permanent identity keys.
Proof verification and conditional consumption must be integrated into one
race-safe enrollment state machine; callers must not treat standalone proof
verification as admission. There is no operational `--join` command yet.

### Administrator invitation command (issuer side)

The joining server logs its UUID with instructions for the cluster administrator. An authenticated admin or owner on an HTTPS/WSS client connection can issue
`/link invite <joining-server-UUID>`. The server saves a pending
invitation in the local SQLite database and replies directly to that operator
with a base64-encoded RUSTYRIG-JOIN-2 token bound to that joining UUID. Treat the response as a private
credential: do not paste it into public chat, logs, shell arguments or issues.
The issuer's own bootstrap URL is derived from station.name and net.http.tls-port; station.name must resolve from the joining server. Default lifetime is 72 hours unless the server's invitation-expiry setting
changes. The command records only the administrative action in audit logs,
not the credential.

**Enrollment is not yet operational.** The joining node's startup token
parser, TLS exporter/channel binding, issuer's fresh challenge and permanent
identity proof, atomic redemption/admission, strict reconnect and signed
catalog sync must be wired together. No invitation may bypass the normal
identity-before-catalog gate.
