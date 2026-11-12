# Reconnect intervals and administration

Server links use `[general] links.retry-seconds=60`. Each enabled `[peer]` or
`[peer:name]` can set `retry-seconds=1..3600`; an absent or empty value inherits
the global interval. Initial autoconnect happens immediately. Failed or lost
links wait the configured interval before retrying.

Authenticated admins and owners can use `/link list`, `/link open <name>`,
and `/link close <name>`. Names are stored peer section names, or the portion
after `peer:`. The unnamed `[peer]` is addressed as `peer`. OPEN requires a
stored outgoing URL and enables retries for the running process. CLOSE drops
the link and blocks both outgoing retries and incoming redials until OPEN or
server restart. Commands do not rewrite configuration or bypass authentication,
ACLs, or tree admission.

Native client connections use `[general] server.reconnect-interval=60`, with
`server.reconnect-interval` in `[server:name]` as an optional override. Values
are seconds, 1..3600; invalid values fall back to 60. The existing ten-attempt
limit and explicit disconnect behavior remain. Browser configuration has a
global reconnect interval and an optional current-server override, stored
locally per WebSocket URL.

IRC numeric 433 emits `irc.nick-in-use`; it has no effect on RustyRig login
names. GTK prompts for a replacement nickname for 45 seconds. Closing the
prompt leaves the timer running. TUI prints an error suggesting `/nick` and
starts fallback immediately. Fallback tries each of `_{}[]-^`, then every
two-character combination, truncating the original nickname to fit NICKLEN.
A rejected replacement chosen by the user uses that replacement as its base.
After all 56 alternatives are rejected, the connection closes without retrying.
An accepted nickname, successful registration, or disconnect clears recovery.

GTK and browser PTT controls latch an orange `TOT EXPIRE` warning after server
TOT shutdown. State updates do not clear it. The next click acknowledges
locally without sending a PTT command; subsequent clicks work normally.
GTK PTT shortcut acknowledgement also consumes repeats and release.

The Admin page requests SQLite users, rooms, quotas, audit events, and PTT sessions.
Inventory includes offline and locked users and deleted rooms. Edit changes email, maximum sessions, enabled status, privileges, password expiry
(Unix seconds, blank means never), and required password change. Account name,
UID and password-set timestamp are read-only; Reset password uses the existing
server command. Lock/Unlock and Remove/Restore retain server permissions.
Admin pages are named Users, Rooms, Quotas, Audit Log and PTT Log.
The server provides TX seconds and separate `bandwidth_tx_used` and
`bandwidth_rx_used` byte totals. GTK displays TX remaining/used in minutes and
bandwidth as B, decimal KB or MB (never larger units), grouping large values
with commas. Its headers are name, TX Min, TX Used, BWRemain, BWTxUsed and
BWRxUsed. TX bytes are sent by the server to the user; RX bytes are received
by the server from the user, matching the existing traffic counters. Both
directions still consume the shared remaining bandwidth allowance. `/quota BW LIST` also reports the directions separately. SET/ADD actions use existing
TX duration syntax (30m, 2h) and decimal BW units (10M, 1G).
Only SET prompts mention that zero exhausts the allowance; ADD prompts do not.
Audit and PTT sections are titled Audit Log and PTT Log.
Remove requires confirmation. The first room removal is reversible; removing
an already-deleted room warns that it permanently deletes metadata and all chat
history. The GUI uses the same session-bound, expiring server challenge as CLI
removal. Recording files and PTT logs are retained. Accounts never expose password hashes.
Each response page contains at most 100 rows, newest first. Load older / more
uses an exclusive row ID cursor, avoiding duplicate rows when logs grow.
Refresh starts a new listing. GTK binds each table and action to its issuing
server profile; browser actions use its current connection.

`/admin users|rooms|quotas|audit|ptt [before-id]` provides the same inventory in TUI.
The server authorizes each request independently. Additive `talk.link` and
`talk.admin` operations are registered in the authoritative wire schema.
Admin responses use `talk.target` for the table, `talk.action` for
`begin|row|end`, and JSON text in `talk.data`. BEGIN contains `append`; ROW
contains selected database fields; END contains `next` (0 means no more rows).
SQLite remains owned by rrserver. Libraries dispatch application events and
frontends present the results.


## Explicit noob supervision

A noob's first PTT attempt in a TX room requests supervision; it never keys RF.
Eligible logged-in elmers receive a GTK/browser dialog or TUI alert in that TX
room. Accepting explicitly acknowledges responsibility for the noob's actions
and being the station's recognized control operator. TUI uses
`/supervise accept|decline <request-id> <rig-room>`; decline also withdraws
previous consent. Noob PTT controls show a clickable grey REQUEST while consent
is missing. Approval restores normal PTT; the noob must press it again to transmit.
The orange TOT EXPIRE acknowledgement takes precedence over REQUEST.

Grants belong to one noob login session and TX room, not to the username or
persistent account flags. Separate clients sharing the same username need
separate approvals. Multiple elmer sessions may accept; losing the last active
supervisor revokes access and releases PTT, retrying if the backend fails.
`[general] noob.supervisor-idle-seconds` defaults to 300. Deliberate chat, CAT
setters, property writes and supervision replies count as operator activity;
polls, keepalives and audio frames do not. An expired grant requires fresh
consent. Pending requests are offered to newly logged-in elmers as well.

The authoritative rig server owns consent, including relayed noob/elmer sessions
present there. `talk.supervise` replies include action, request ID in target,
TX room, requesting user and explanation in data. Replies use data
`accept|decline <id>` with the same room. Libraries emit application events and
fail closed when no server supervision listener exists. IRC is unaffected.

Native SOCKS5 failures name the credential-free proxy endpoint and destination,
with the specific rejection, authentication or timeout reason. Proxy passwords
are obscured in displayed error text; a disconnect before tunnel setup also
produces an explicit proxy failure notice.

GTK Users gives spare width to Privileges and keeps Chg Req fitted to its
header. Rooms and Quotas give spare width to name. PTT Log shows the transmitting
user's home server as its second column, duration as TxLen, grouped frequency
and a server-owned rig/VFO scope such as `site.rig2.vfo-a`. New PTT rows snapshot
that scope and the user's origin server (including linked sessions) in the
node-local database. Existing databases gain a nullable server column; old
origins and unscoped historical rigs display as unknown in GTK. The server's
existing admin results expose the fields to all clients, including WebUI.
Config actions occupy one horizontal row, followed by one blank text row
before the bottom main-tab navigation to reduce accidental clicks.

## TLS hostname selection, self-signed trust, and mandatory SPKI pinning

The server editor has two short, independent checkboxes:

- **Ignore TLS hostname** (default off): use the configured **Node Name**
  (such as `rplywv00.ham.istabpeople.com`) as the certificate DNS SAN identity
  instead of the dialed URL hostname/IP address. It does **not** disable
  certificate trust checks or SPKI verification. With no expected Node Name
  yet, the client may learn one from an explicitly approved first-use key
  and the authenticated station/domain identity, provided the certificate's
  DNS SAN matches it exactly.
- **Allow self-signed certificates** (default off): permit explicit
  first-use SPKI approval without a trusted CA. This is a separate opt-in
  trust policy; enabling Ignore TLS hostname does not enable it.

On an initially CA-verified WSS connection, RustyRig waits for a validated,
authenticated station/domain/UUID announcement, then saves the observed
TLS public-key SPKI SHA-256 fingerprint to the profile database. This also
works for a certificate with an IP SAN only: the IP SAN validates the
connection, while the authenticated station/domain establishes the mesh
identity; a DNS Node Name is only learned when it matches a DNS SAN. New
connections enforce this fingerprint even when ordinary CA validation
would succeed. An already pinned key is never silently overwritten.
An IP address may be used to dial the server while the Node Name selects
the expected certificate SAN. Certificate renewal with an unchanged key
retains the pin.

On a self-signed initial connection, the client stops **before sending
credentials** and presents the certificate CN, DNS SANs, SHA-256
certificate and SPKI fingerprints. The operator must independently verify
the key fingerprint before approving. **Accept Once** is temporary;
**Accept Always** persists the public-key pin. A changed pinned key
causes an explicit approval prompt; Deny leaves the old pin intact and
automatic retries stop. Neither checkbox silently resets a saved pin.

RustyRig matches the expected hostname against the DNS or IP SAN
(depending on name type) when a pin is used. It also checks the authenticated
node announcement against the configured Node Name and rejects a mismatch.
A public-key directory can eventually provide out-of-band verification,
but it must itself be trustworthy. On headless clients a changed key is
rejected rather than silently accepted.

The profile properties are `server.tls-ignore-hostname`,
`server.tls-trust-on-first-use`, `server.node-name`, and
`server.tls-spki-sha256`.

## Mandatory account password changes

When the server sends `auth.password-change-required=true` on successful
authentication, the GTK client opens a **Password change required** dialog.
It requests and confirms an 8–256-character new password. The client sends
`auth.cmd=change-password` and `auth.new-password` only over an
authenticated encrypted connection, rather than issuing a chat `/user pass`
command.

The client does **not** update the saved server profile password when the
request is sent. It waits for the server's explicit `auth.cmd=password-changed`
reply. Only after that response does it save `server.pass` to the client
SQLite database, notify the user, disconnect, and reconnect using the new
password. A `password-change-failed` response retains the original password
and offers Retry. Losing the connection before confirmation leaves the saved
password unchanged and reports uncertainty; the operator may need to update
the profile manually if the server committed the change but the reply was lost.

### Learning the expected TLS node name

For a RustyRig WSS profile with no configured `server.node-name`, approving
an unknown TLS identity does **not** immediately assign a name. After the
server connects and authenticates, the client checks its station/domain
announcement and server UUID against the connected identity, then requires
the resulting `station.domain` to appear exactly in the certificate DNS SAN.
Only then is `server.node-name` saved to the client database. Existing
expected names are never overwritten. This also works for a profile whose
URL is an IP address (for example, `wss://127.0.0.1:4420/ws/`), so future
connections can verify the expected DNS identity explicitly.
