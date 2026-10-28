# Native mesh WSS

Linux server links use OpenSSL, wslay and the GLib reactor on TCP **4420**.
TCP **4422** retains Mongoose client HTTPS/WSS and one-shot enrollment.
`rr_transport` remains platform independent. QUIC, WebTransport, MCU support,
automatic discovery of non-issuer neighbors and multipath forwarding remain
future work; existing tree admission/routing and topology ACLs are retained.

## Operator setup

Install the OpenSSL, wslay, GLib/GIO development packages when building
(`libssl-dev libwslay-dev libglib2.0-dev` on Debian). Each server needs:

```ini
[general]
domain=my-domain
station.name=unique-site-name
links.native.enabled=true
links.native.bind=0.0.0.0
links.native.port=4420
links.listen=
path.identity=/var/lib/rustyrig/identity
net.http.tls-cert=/var/lib/rustyrig/identity/tls-cert.pem
net.http.tls-key=/var/lib/rustyrig/identity/tls-key.pem

[listener:link]
acl=server-links
[acl:server-links]
allow=10.8.0.0/24
deny=all
```

Use your actual peer network in the ACL. It also controls outbound enrolled
peer admission; an empty/missing ACL denies admission. Explicit configured
peers additionally retain their own peer ACL. `links.native.enabled=false`
disables listening only. `net.mesh-linking=false` disables enrolled automatic
connections/admission while preserving explicitly configured peers.

Run `rrserver --mesh-join` on the joining host to obtain its persistent UUID.
On the issuer, use `/link invite UUID [issuer-IP:4422]` over authenticated
HTTPS, and securely deliver the resulting one-use token to the joining host.
Complete `--mesh-join`, then restart the joining server. Both peers prove their
identities automatically; no `[peer]` password is necessary. `/link list`
shows entries as `enrolled:UUID`. `/link close enrolled:UUID` and `/link open
enrolled:UUID` retain the existing administrative semantics (incoming-only
entries without a dial URL cannot initiate a connection).

`mesh_bootstrap_peers.url` remains the enrollment endpoint. Successful status
responses over pinned HTTPS advertise the issuer's native port, stored
separately in `mesh_link_endpoints`. The host remains the invited host. A stored endpoint matching the previous
default pair (enrollment 4420, mesh 4421) is migrated to mesh 4420. Old
enrollment records without this table derive `wss://invited-host:4420/link`,
never the enrollment port. Outstanding tokens naming the old HTTPS port 4420 must be reissued after
upgrading the issuer; previously enrolled identities keep their trust. Clients
using an explicit 4420 HTTPS URL must switch to 4422. NAT deployments must expose the configured native
port at the invited host. Re-enroll/reconfirm the consumed invitation after a
port change, or deliberately update the local endpoint record; TLS pin and
identity checks still apply to that endpoint.

The private signing key lives under `path.identity` (0700 directory, 0600 key).
Keep it and the SQLite database persistent and distinct on each server. Native
TLS uses the **same** certificate/key as enrollment, preventing accidental
pinning of one listener's key while serving another. Old `links.listen` causes
an actionable startup error; clear it and use `links.native.*`. Shared HTTPS
`/link` returns 426; neither `links.shared-listener` nor `links.allow-insecure`
enables a downgrade. Existing ordinary `/ws` clients are unaffected.

## Authentication and ownership

1. Both TLS endpoints must present a certificate. OpenSSL validates TLS key
   possession; a custom certificate verifier validates certificate dates,
   minimum 112-bit key strength and SHA-256 of DER SubjectPublicKeyInfo against
   explicit trust in the local domain. Outbound TLS also requires the expected
   destination pin. Self-signed leaves are supported because enrollment pins
   the key; public CAs and hostnames are not the mesh identity authority.
2. TLS is at least 1.2; TLS 1.2 requires Extended Master Secret. Compression,
   renegotiation and session resumption are disabled. A 32-byte exporter under
   `EXPORTER-RustyRig-mesh-v1` binds the subsequent proof to this TLS session.
3. `/link` must negotiate `rustyrig-mesh.v1` through a bounded RFC 6455
   upgrade. Each endpoint sends `mesh.auth=challenge-v1` with a fresh 32-byte
   random base64 `nonce`. It accepts exactly one remote challenge.
4. Each endpoint sends `mesh.auth=proof-v1`, signed with the existing canonical
   Ed25519 identity signer. The signed dictionary includes `identity.domain`,
   `identity.owner` (UUID), `identity.public-key`, `site`, its own `nonce`, the
   remote `challenge`, exporter `channel`, and `role` (`initiator`/`acceptor`).
   The receiver checks every binding, the signature, and the exact enrolled
   `(domain, UUID, catalog key, live TLS SPKI)` tuple. Duplicate, reflected and
   cross-connection/replayed proofs fail before link admission. A proof is at
   most 4096 bytes and shares the connection's handshake deadline.
5. Only then may `link.hello` reach normal admission. Its UUID/site must match
   the verified proof. The legacy `password` wire field is empty and has no
   authentication role. Snapshot/ready, branch ownership, relay authorization,
   topology restrictions and duplicate-connection arbitration remain in the
   existing link implementation. The old static `link.identity` proof is not
   accepted as live authentication.

SQLite trust is queried on each handshake, and every ten seconds on an active
link. Deleting/changing an enrolled trust row prevents new links and closes an
established link at its next heartbeat check. Catalog refreshes cannot authorize
new direct peers. Manually configured `catalog-key`/`tls-trust` pairs remain
explicit trust anchors, independent of enrollment rows.

Transport handles are borrowed from the WSS driver. Close is deferred until the
current callback unwinds; CLOSE releases the link session and the driver then
reclaims its transport/socket/TLS/framing state. DNS is asynchronous. TLS WANT
states, stable write retries, partial reads/writes, frame fragmentation and
automatic ping/pong use readiness callbacks. Receive messages are bounded to
32768 bytes, output to 262144 bytes and 256 queued frames, HTTP to 8191 bytes,
and in-flight sessions to 32. Per-turn work is bounded to preserve client-loop
progress. Handshake and heartbeat failures log their cause and retain the
existing randomized automatic retry policy.

## Validation

`bash rrserver/tests/test_mesh_native_live.sh` launches separate real servers,
keys and databases on temporary loopback ports. It uses real `/link invite`
and `--mesh-join`, checks restart/autoconnect, `/link list`, catalog delivery,
client-session continuity, reconnect, and link reconnect.
An independent OpenSSL test client checks invalid certificates, signatures,
UUID/domain/channel/role bindings, replay/reflection, pre-authentication data,
timeouts, oversize frames, no plaintext/Mongoose fallback, revocation and ACLs.
The suite is discovered by the existing `rrserver` test-runner directory scan.

The component unit suites additionally cover transport close lifetime,
enrollment, strict link wire decoding, tree admission and mesh route selection.
The older `links_live.py` media harness predates enrolled identities and the
mandatory TLS migration; its password/plaintext fixture requires a separate
update before it can validate direct/proxy media on this transport.
