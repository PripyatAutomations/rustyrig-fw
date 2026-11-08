# Building, rebuilding and recovering a RustyRig cluster

This runbook applies to the native TLS/WSS mesh (TCP 4420). Perform disruptive
operations during a maintenance window on **every** node; partial resets leave
old trust entries, advertised identities, and clients out of sync.

> **Security boundary:** the mesh Ed25519 signing key, the mesh TLS key,
> the server UUID, the enrollment database, and each client's Ed25519 key are
> distinct. Resetting only one of them does **not** rotate the others.
> A database reset is **not** a certificate reset.

## 0. Inventory and preserve evidence

Stop the server/client processes and prevent exposed old nodes from reconnecting.
For a suspected breach, preserve immutable/offline forensic copies **before**
deleting or rotating material. Treat these backups as compromised if the attacker
could read the keys. Do not restore old credentials into the rebuilt cluster.

Confirm the exact effective `path.db.master`, `path.identity`, TLS cert/key
paths, `station.name`, and `domain` in your installed configuration; the paths
below are package defaults. A server with another configuration file may have
a completely different database and identity directory.

Record the intended topology and service URLs/IPs. Make a deliberate choice
of **new** domain or the existing domain; all new peers must use the same domain.
A new domain is strongly recommended for a post-compromise clean break.

## 1. Prepare packages and names

Install the same current RustyRig packages on each server and client. Rebuild
and install independent libraries before RustyRig itself:
`librustyaxe`, `librrprotocol`, then `rustyrig-fw`.

Each server uses a unique single-label `station.name` and the chosen shared
`domain`. The expected DNS SAN is `station.name.domain`:

```ini
[general]
station.name=rplywv00
domain=ham.istabpeople.com
net.http.tls-port=4420
net.http.tls-auto-generate=true
net.http.tls-cert=/var/lib/rustyrig/identity/tls-cert.pem
net.http.tls-key=/var/lib/rustyrig/identity/tls-key.pem
```

This is a **logical TLS verification name**, not a requirement for DNS.
A client can dial `wss://127.0.0.1:4420/ws` (or the WireGuard IP) and
configure `server.tls-name=rplywv00.ham.istabpeople.com`.
The certificate must contain that exact SAN, and the client must trust its
issuer or the explicitly provisioned certificate. Changing the URL alone
never bypasses certificate verification.

Do not expect a previously issued `rplywv00` certificate to match the
new `rplywv00.ham.istabpeople.com` name. The certificate provisioning
routine **refuses mismatched existing certificates**; it does not silently
rotate a trusted keypair.

## 2. Rebuild each server's database and identity

For a clean rebuild where all former identities are intentionally retired:

1. Stop `rrserver` on **all** hosts. Restrict the old listener while rotating.
2. Back up forensic material securely and inventory non-key data worth
   preserving (users, room configuration, and logs) separately.
3. Run the supported database maintenance command on the **stopped** host:

   ```sh
   sudo rrserver -f /etc/rustyrig/rrserver.cfg --reset-db
   ```

   It requires an interactive terminal and the exact confirmation
   `YES --reset-db`. It erases users, authorization, room/rig identity
   mappings, history and stored trust, **not TLS/Ed25519 PEM files**.
   `--reset-uuids` alone is not sufficient for an incident-response rekey.

4. **Only if intentionally replacing the server identity**, move the three
   PEM files out of `path.identity` into an isolated offline incident archive.
   Do **not** put the private keys in the network-accessible SHARED tree,
   client configuration, or Git repositories. Example, with the default paths:

   ```sh
   sudo sh -c '
   set -eu
   archive=$(mktemp -d /root/rustyrig-old-identity.XXXXXXXX)
   chmod 0700 "$archive"
   mv /var/lib/rustyrig/identity/server-ed25519.pem \
      /var/lib/rustyrig/identity/tls-key.pem \
      /var/lib/rustyrig/identity/tls-cert.pem "$archive/"
   echo "Previous keys archived under $archive"
   install -d -o rustyrig -g rustyrig -m 0700 /var/lib/rustyrig/identity
   '
   ```

   Use a **unique** secure archive directory per node and do not overwrite
   previous backups. In confirmed compromise, isolate old keys for investigation
   and dispose of them under the incident-response policy. Do not distribute
   archived private keys as trust anchors.

5. Reprovision the new master DB with a fresh strong administrator password
   according to the package installation/first-run procedure. In packaging,
   the post-install setup handles new DB provisioning; a raw runtime reset
   can require rerunning that initialization. Verify DB ownership before launch.
6. Start each server once and check for fresh Ed25519/TLS identities, the
   expected SAN and the native TLS listener. Verify restrictive permissions:

   ```sh
   sudo sh -c 'stat -c "%U:%G %a %n" /var/lib/rustyrig/identity/*'
   sudo stat -c '%U:%G %a %n' /var/lib/rustyrig/identity
   sudo openssl x509 -in /var/lib/rustyrig/identity/tls-cert.pem \
       -noout -ext subjectAltName -fingerprint -sha256
   ```

   The directory must be owned by `rustyrig` and mode `0700`, and
   private keys must be `0600`. Retain the generated server UUID and
   the **public** certificate/SPKI fingerprint in the operator inventory.

**Partial rotation:** If only the TLS certificate changes while the master
DB/signing identity survive, the old mesh TLS SPKI pins remain and peers
will reject the replacement. Re-enroll/re-pin the affected server using
an independently verified new fingerprint. If the signing key is believed
compromised, preserve neither its trust nor its old authenticated identity.

## 3. Bootstrap the new mesh

Choose one server as the initial issuer. Confirm the nodes share the configured
domain, ACLs allow the intended peer addresses, and TCP 4420 is reachable.
Existing `[peer]` blocks may be disabled when using enrollment.

On a joining node, obtain its UUID using the supported `--mesh-join` flow.
On the issuer, an authenticated administrator runs:

```text
/link invite JOINING-SERVER-UUID
```

Securely transfer the one-use invitation token to the joining host. Complete
`rrserver -f /etc/rustyrig/rrserver.cfg --mesh-join` using the token
(interactive prompt, or the optional token-file argument). Verify the issuer's
TLS SPKI fingerprint through an **independent trusted channel** before
accepting the invitation. Never trust a fingerprint supplied only by an
untrusted host/connection during incident recovery.

Restart the joining server. Repeat for the remaining nodes and verify
`/link list`, `Loaded enrolled issuer`, `Autoconnect dialing peer`,
verified live identities, and catalog synchronization. The enrolled issuer
is loaded from `mesh_bootstrap_peers` for the **exact** `domain` only.
The default autoconnect startup delay is five seconds. The current
forwarding model is tree-based, not unrestricted all-to-all links.

## 4. Reset and reconnect client machines

Clients retain their own SQLite and key state separately from server state.
Stop `rrclient` first.

- Client profile/theme database: `$XDG_STATE_HOME/rustyrig/client.db`, or
  `~/.local/state/rustyrig/client.db` when `XDG_STATE_HOME` is unset.
  Back up and then remove/rename this SQLite database (including any
  `-wal` and `-shm` companions) **only when intentionally resetting all
  saved profiles, settings and themes**.
- Client Ed25519 identity: `$XDG_CONFIG_HOME/rustyrig/identity/client-ed25519.pem`,
  or `~/.config/rustyrig/identity/client-ed25519.pem` by default.
  Use the GTK key manager to create/import/delete identities. For a full
  client credential rotation, retire the previous private key and generate
  a new one, then register/authorize the **new public key** on each server.
  Merely removing the client database does not rotate its private key.
- Recreate server profiles with the desired transport URL, a separately
  configured `server.tls-name`, and the public certificate or CA trust path
  (`server.tls-ca` or global `tls.ca-file`). Keep the trust file readable
  by the client; **never** copy the server's TLS private key to a client.
- Reset server-side user credentials, access grants and any potentially
  compromised client/session credentials. Re-authorize user certificates
  and permissions from a known-good inventory. Disconnect old sessions.

## Certificate operation boundary

The GTK Admin > Security & Trust page is for read-only identity inventory
and **peering configuration removal** (`peer-remove`) only. Removing
peering does not revoke a signing key, TLS certificate, or catalog, and
does not authorize the client to rotate keys.

No remote `identity-revoke` command is supported. Server cryptographic
revocation and rotation are reserved for local, stopped-server maintenance.

The new `rrserver --revoke-certs` and `--rotate-certs` maintenance commands
require a stopped server, the exact interactive `YES --<option>` confirmation,
and an existing master database containing the persistent server UUID.
Run them with the server's usual `-f` configuration and the filesystem account
that owns the private identity. Both **sign and durably queue** a revocation of
the *previous* signing key and TLS SPKI before archiving its PEM files.

- `--revoke-certs` retires keys and places `identity/revoked.lock`, deliberately
  preventing normal startup until a later local rotation.
- `--rotate-certs` retires the prior keys and leaves the normal auto-generation
  path to provision a fresh signing/TLS keypair at next startup. When recovering
  a previously revoked server it removes `revoked.lock` after verifying the
  active PEM paths have been cleared.
- Retired private PEMs are archived in a private `identity/retired-...`
  directory for incident forensics; protect or securely destroy the archive
  under the incident-response policy.
- Existing peers will initially reject the **new** keys until separately
  authorized enrollment updates their trust. The queued signed notice can
  replay over newly established, authenticated links. Old keys remain denied.

**Important operational limitation:** these one-shot commands do not yet
establish outbound network sessions *while the old keys remain active*.
The durable revocation ledger is replayed after the new identity successfully
connects. Received revocations are authenticated, persisted, forwarded, and
acknowledged by direct peers; ACKs are not proof that every remote node has
converged. Do not claim cluster-wide completed revocation until every intended
node has accepted the new denylist and the stale catalog is gone. Full
adversarial integration testing remains required.

Catalog resource upserts/removals and user-directory snapshots have
end-to-end signatures and persistent revisions/replay protection. Relay
commands, replies, subscriptions, administrative state and revocation ACKs
carry origin-server signatures bound to the operation, destination and
session, with a timestamp and persistent one-use nonce. Relayed room state
is signed by its *owning* server and forwarded without intermediary
re-signing; a rogue intermediate cannot claim to be another state owner.
Retired signing keys and TLS SPKI fingerprints remain denied. A server's
existing explicit enrollment takes precedence over historical first-seen
catalog pins. **First-seen
catalog keys for previously unknown UUIDs remain a bootstrap weakness**:
without a cluster-authorized membership registry, a malicious peer can claim
an unregistered UUID first. Treat unfamiliar nodes as untrusted and verify
membership identities out of band.

## 5. Validate and revoke

Verify every server and client against fresh fingerprints/UUIDs and the correct
logical TLS hostname. Confirm a host with the wrong domain, untrusted SPKI,
invalid credential, or former client key is rejected. Test reconnects,
catalog synchronization and authorized remote operation.

For post-breach recovery, rotate any exposed external secrets too
(WireGuard keys, API credentials, password hashes/passwords, SMTP credentials,
backup access, and administrative tokens as applicable). Remove compromised
hosts from the network until patched or reimaged. Log/archive the date, reason,
new public keys and the list of revoked credentials.

## Troubleshooting

- `Cannot securely load or create server Ed25519 identity`: check owner/mode
  of the private identity directory and PEM; packaging must not grant admin
  ACLs recursively inside that directory.
- `TLS identity provisioning failed`: check key mode `0600`, matching
  keypair and certificate SAN; an existing wrong-name certificate is **not**
  automatically overwritten.
- `No configured or enrolled peers`: query `mesh_bootstrap_peers` and
  compare its domain to the live `domain=` configuration. Do not edit trust
  rows to force a match.
- Native client `TLS verification failed`: distinguish the URL dial address,
  `server.tls-name` identity, certificate SAN, and `server.tls-ca` trust.
  Disabling verification is not a recovery strategy.

See [server maintenance](server-maintenance.md) and
[native mesh operation](native-mesh.md) for details of the existing commands
and live protocol.
