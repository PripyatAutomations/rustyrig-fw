# TLS password authentication and Argon2id migration

Status: **merged to `main` in all three repositories**. This is an intentionally
breaking authentication protocol update, pending clean-build and integration
validation before deployment to production stations.
Repositories: `rustyrig-fw`, `librrprotocol`, and `rustyrig-www`.

## New protocol

An enabled account connects over a verified `wss://` connection, requests
`auth.cmd=login`, and receives `auth.cmd=challenge` with
`auth.method=tls-password-v1`, a nonce and a session token. The client
responds with `auth.cmd=pass`, `auth.method=tls-password-v1`, the original
password, account, and token **inside the encrypted connection**.
The server accepts a single challenge response per connection.

All new passwords are hashed in SQLite with Argon2id v=19, a unique random
16-byte salt, 19 MiB of memory, 2 passes, 1 lane, and a 32-byte output.
The encoded hash contains its parameters and salt. The server verifies the
complete verifier directly from SQLite; `http_user.pass` is a legacy
40-byte field and is not an Argon2id password-verification source.

The server accepts **no** legacy double-SHA1 challenge responses. For an
existing account with a 40-character SHA-1 hash in SQLite, the user can
still supply their **original plaintext password over WSS**. Upon successful
legacy verification, the account is immediately rehashed with Argon2id.
Normal users must then choose a new permanent password; the explicitly
shared guest account is exempt from that forced-change requirement.
If the original password is unavailable, use the explicit admin password
reset rather than trying to convert the old hash.

No password is placed in an HTTP URL, WebSocket query parameter, login
notice, or log message. Keep TLS listener verification enabled. In the
browser, both the page origin and the active WebSocket must be HTTPS/WSS.

## Changing passwords from the browser

The standalone `rustyrig-www` client has a Password tab and a dedicated
password-change dialog. Forced first-login changes are prompted after
`auth.cmd=authorized`. The form sends `auth.cmd=change-password`
with `auth.new-password` over the existing authenticated WSS session.
The server validates the session token, TLS, account identity, and password
length (8–256 bytes/characters as represented by the protocol), then updates
SQLite and responds with `auth.cmd=password-changed`. It never passes the
new credential through chat commands or chat history.

## Packaging and coordinated rollout

The standalone `librrprotocol` now depends on `libargon2` and requires
`libargon2-dev` to build. Reinstall that library, then rebuild the server
and client, deploy the paired web UI, and restart after a coordinated
maintenance window. Debian's first-install and administrator reset paths
must write Argon2id, not SHA-1. Package bootstrap now uses the private
`/usr/lib/rustyrig/rr-password-hash` tool (password on stdin, encoded hash
on stdout).

Existing clients sending legacy SHA-1 wire responses cannot log into a
new-protocol server. This is deliberate; there is no weak-protocol fallback.
The updated native and browser clients are needed at the same time.

## Release blockers

- Build with clean installed `libargon2-dev`, `librustyaxe-dev`, and
  `librrprotocol-dev`; run protocol, SQLite and packaging regression tests.
- Convert existing live integration tests using plaintext `ws://` and
  SHA-1 wire challenges to temporary TLS listeners using new login messages.
- Test native login, browser login, forced/self-service password changes,
  incorrect-password lockout/rate limits, concurrent sessions, account
  disable/expiry, migration, and downgrade attempts.
- Review TLS credential handling/redaction on all message tracing and ensure
  that failed changes are displayed in the browser without logging out.
- Add session revocation/short-lived credentials and certificate-backed client
  login before claiming the entire long-term authentication redesign finished.

Update `librrprotocol0` (at least `0.1.0-6`) first, then server and native
clients (at least `rustyrig-fw 1.0.2-11`), and deploy the updated
`rustyrig-www` together. Existing SHA-1 wire clients will be rejected.
Back up the account database before upgrading and confirm the new client
works over WSS before disconnecting an existing administrator session.
