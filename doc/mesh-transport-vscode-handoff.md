# Mesh transport handoff — 2026-10-28

Update: the native WSS transport and enrolled startup connections are now
implemented. The native TCP 4420 listener now accepts mesh links, client WSS and enrollment; the compatibility HTTPS listener remains on TCP 4422. See [current operation/security](native-mesh.md) and the live
integration suite for the authoritative state. The remaining text is the
original pre-implementation handoff, retained as historical context.

## Objective
Replace Mongoose specifically for server-to-server linking first, using a portable `rr_transport` core, GLib event loop on Linux, OpenSSL for TLS, and wslay for WebSocket framing. Preserve existing client HTTPS/WSS on TCP 4420 while introducing authenticated server-link WSS on TCP 4421. Once mesh links and failover work, implement multihomed user/client connections. Later consider ngtcp2/nghttp3 and WebTransport (UDP 4421). ESP32 size/feature trimming is explicitly deferred; do not drag GLib into the portable core.

**Working milestone:** Two newly enrolled servers, without manually configured `[peer]` sections/passwords, automatically establish an authenticated TCP 4421 WSS connection on restart, exchange topology and application frames, appear in `/link list`, and reconnect after failures. Both ends authenticate the live transport and catalog identity. Tests should cover bad keys, pin mismatch, replay, timeouts, reconnect, and no downgrade.

## Repositories/branches
- Parent: `PripyatAutomations/rustyrig-fw`, branch `mesh-linking`.
- Protocol submodule: `PripyatAutomations/librrprotocol`, branch `crypto-backend`, parent gitlink currently at `53cd6ce0e58a32263ad3513daaa3e316674d8a2f`.
- `ext/wslay` already exists as a submodule in `.gitmodules`.
- `www` submodule was removed from parent (its independent repository remains untouched).
- Work in C, build with `make -j$(nproc)`; user uses Devuan/Linux, GTK3, GLib; production-like installs with `sudo make install`.

## Successfully completed / confirmed
- CLI `rrserver --mesh-join` prints the persistent server UUID and tells operator to send it to an issuing mesh admin. Invitation token can be pasted visibly in noncanonical terminal mode (avoids canonical TTY length truncation).
- On issuer, `/link invite <joining-UUID> [issuer-IP:port]` makes a one-time, private enrollment token. Default address chooses configured HTTPS bind or kernel route toward admin, port `net.http.tls-port` (typically 4420); override useful across VPN/NAT.
- Enrollment uses libcurl HTTPS, issuer TLS SPKI pin, Ed25519 signed challenge/proof and durable SQLite trust. Issuer audit observed: `Enrolled server identity 5bd92d3b-0108-4594-adc5-0cab40ef5f8c via single-use invitation`; joining side printed `Mesh enrollment: server identity admitted and issuer trust stored.` That is proof of *identity enrollment*, **not** an operational mesh link.
- Initial `/mesh/enroll/status` returns 404 until redemption; expected, now suppressed. HTTP responses had double-escaped CRLF/newline bugs; fixed.
- Enrollment writes `mesh_bootstrap_peers` (domain, node_uuid, url, catalog_pubkey, tls_spki_sha256, state) and `mesh_explicit_trust`. Parent `rrserver/link.c` currently loads enrolled issuer records into peer table, but blocks autoconnect deliberately because live transport/authentication is not implemented.
- HTTPS/WSS is the only intended HTTP server listener; plaintext HTTP/WS removed in `librrprotocol/srv.http.listeners.c`.
- MQTT code/tests moved into `rrserver/disabled/`; MQTT removed from active build/startup. `www` removed from Git submodule and install/package recipes made optional.
- `librrprotocol/transport.h` and `transport.c`: new platform-independent, capability-based `rr_transport` ABI (reliable messages, streams, datagrams, TLS-authenticated/SPKI capabilities). **No connection backend uses this yet**.
- Application periodic timers moved from `mg_timer_add` to GLib `g_timeout_add`. `rrserver/reactor.glib.[ch]` wraps GLib fd readiness and main-context dispatch.
- `rrserver/mesh.native.listener.[ch]`: opt-in TCP 4421 raw socket listener, backed by GLib; **currently closes all incoming connections immediately**. Config: `links.native.enabled=false` default; `links.native.bind=0.0.0.0`; `links.native.port=4421`. Absolutely do not treat this as usable WSS/TLS.
- User reported build success through GLib timer migration; **later commits including GLib reactor/4421 listener have NOT been build-tested here**.

## Incomplete / highest priority
1. Implement nonblocking OpenSSL TLS (using the existing server identity cert/key) + wslay WSS handshake/frame driver on GLib reactor, on TCP 4421. Do not enable plaintext fallback. Handle handshake, reads/writes, partial writes, backpressure, nonblocking WANT_READ/WANT_WRITE, timeouts, cleanup, ping/pong. Build/test.
2. Expose verified *live* peer TLS certificate SPKI to `rr_transport`; do not trust self-reported SPKI. Outbound client must compare expected pin from `mesh_bootstrap_peers`. Issuer must authenticate inbound peer against enrolled trust.
3. Implement mutual Ed25519 challenge/response proving current possession of catalog identity key, bound to the verified TLS connection and fresh nonces. Existing `link.identity` message proves only the key that signed the static TLS/SPKI binding, **not** a live key proof. Check for replay and mismatched UUID/domain/pin/catalog keys, fail closed.
4. Adapt existing `rrserver/link.c` session lifecycle, `link.hello`, `link.identity`, snapshot/ready, clock/health, routing, retry and ACL handling to transport-neutral callbacks. Existing code depends extensively on `struct mg_connection`, `mg_ws_connect`, `mg_ws_send`, `mg_tls_init`, and shared passwords. Prefer incremental backend integration over rewriting routing.
5. Bootstrap autoconnect loads `mesh_bootstrap_peers`; upon successful verified handshake, establish normal link session. Issuer currently stores joining identity in `mesh_explicit_trust`, but inbound old `handle_hello` demands named `[peer]` and a password; replace with secure dynamic admission. Do not invent an identity based solely on site name.
6. Decide enrollment URL (4420 on existing Mongoose endpoint) vs persistent link URL (4421), and store/advertise them distinctly. Today invitation URL refers to issuer HTTPS port 4420. Do not dial 4420 accidentally expecting native 4421 WSS.
7. End-to-end two-server tests including initial join, restart, reconnect, certificates, revoked/untrusted peers, topology sync, and mesh route forwarding. Retain legacy Mongoose linking until new path works.

## Important code pointers
- `rrserver/link.c`: `handle_hello` has peer/site/UUID/password check, `handle_identity` compares signed identity to config PEM, `send_hello`, `send_identity`, `link_callback` handles Mongoose socket/HTTP/WS, `rrserver_links_init` reads config and bootstrap rows, `rrserver_links_poll` manages retry/dial, `/link invite`.
- `rrserver/mesh.enroll.c`, `rrserver/identity.[ch]`: enrollment, signed catalog and permanent trust.
- `rrserver/main.c`: GLib timer callbacks, `mg_mgr_poll` plus `rr_reactor_dispatch_ready`, native listener startup and shutdown.
- `rrserver/reactor.glib.[ch]`: fd readiness GLib adapter. Review lifetime ownership of watch (source data destroy) carefully.
- `rrserver/mesh.native.listener.[ch]`: 4421 opt-in placeholder; replace or extend for real connections.
- `librrprotocol/transport.[ch]`: new API, no GLib/POSIX types; design is preliminary and may need changes for stream IDs, recv backpressure, async ownership/lifetime, peer identity.
- `rrserver/defconfig.c`, `config/rrserver.cfg`, `rrserver/rules.mk`, `.gitmodules` and `mk/install.mk`.

## Risks/observations
- Existing `rr_transport` driver API has simplistic close and opaque channels; audit event callback lifetime and stream mapping before depending on it.
- Current GLib fd watch's source destroy callback frees the watch; beware stale pointer/double cancel if callback returns false; update ownership/cleanup.
- Don't blindly copy SPKI from token into connection identity: TLS backend must verify actual presented certificate public key, then verify application key signature and enrolled trust.
- The issuer's old `handle_hello` checks `equal_secret(peer->password,...)`; disabling this check without a new authenticated proof is a vulnerability.
- `/link invite` chooses an IP based on the admin's route; can differ from joining server's path; retain explicit override, consider address candidate lists later.
- Existing dedicated `links.listen` might use different TLS certificate than `net.http.tls-cert`; avoid signing/pinning one certificate then presenting another.
- Separate transport establishment from authorization and topology acceptance; failure/retry must not poison other peers.
- User wants concrete C changes, full build, and operational milestone; repeated short preparatory commits without a working test are not helpful. Do not claim mesh linking works before it demonstrably does.

## Suggested immediate implementation path
Build a minimal integration harness that starts two distinct server identities/dbs with ports 4420/4421, then implement a WSS server/client handshake transport with OpenSSL and wslay and peer SPKI pin validation. Wire this transport into `rrserver/link.c` via adapter callbacks; add authenticated fresh signed handshake and bootstrap dialing. Make a small protocol test to send a link frame both ways and a full two-server test verifying real topology and reconnect. Keep changes bisectable but target working end-to-end behavior before asking the operator to pull.

