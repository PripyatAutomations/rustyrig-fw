# User-command privilege audit

Reviewed 2026-10-06 against the native command registry, browser parser, server
WebSocket dispatcher, protocol handlers and rrserver event consumers. This table
records the working tree after the policy corrections requested during this audit.
`A` means `admin|owner`; `T` means `admin|owner|tx|noob`; `C` means
`admin|owner|chat`. A pipe means any one privilege. Privileges are comma-separated
account tokens; dotted wildcard tokens such as `media.*` are supported.
Authentication is required for all server commands below. Local UI commands do
not grant server access. `view`, `radio` and `edit` are not general gates: their
presence alone does not authorize CAT or account management.

## Server commands

### Connection

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| Login / password response | Credentials for enabled account | Expired passwords rejected; temporary passwords permit own password change only; reconnect-proof peer limits; logged-in account switches denied |

### Chat and rooms

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| `/join`, native `/j` | Authenticated only for existing rooms | Unknown undashed rooms may be created by any account; dashed creation requires A; deleted rooms cannot be joined |
| `/msg`, messages from `/query` tabs | C | Not muted; private target must be a username; private messages go to endpoints only |
| `/names` | Authenticated only | Roster is scoped to recipient's joined rooms |
| `/part` | Authenticated only | Affects own membership/subscriptions |
| Public text, `/me`, room file chunks | C | Not muted; sender must belong to destination room; recipients are joined sessions only |
| `/room ADD` dashed room / restore | A | Any dash reserves a station namespace, including future sites; base rig rooms server-owned |
| `/room ADD` new undashed room | Authenticated only | Configured site lobby protected; creation audited |
| `/room LIST`, `/list` | Authenticated only | Lists active room names; no membership requirement to discover names |
| `/room REMOVE`, `-f`, `-f -h` | A | Session/room/options-bound confirmation; site and configured base rig rooms protected |
| `/room … VFO ADD/REMOVE` | A | Configured base-room bindings protected; added VFO must belong to room's rig |
| `/room … VFO LIST` | Authenticated only | Reads active room bindings; no membership requirement |
| `/topic` read | Authenticated only | Must belong to room |
| `/topic` write | C | Not muted; must belong to room; empty text is a query, not topic clearing |
| `/whois` | Authenticated only | Any online user; includes email, privileges, user agent and session metadata |

### Radio and discovery

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| CAT frequency/mode/width/power (GTK, browser, raw wire) | T | Not muted; current noob/elmer checks; room/VFO restrictions |
| CAT PTT halt for another user | T plus ownership exception | Admin/owner may halt others; elmer may halt a noob; handler still enters through T gate |
| CAT PTT on/off | T | Not muted; TX room membership; noob/elmer and cooldown checks; ownership/preemption, quota and station interlocks |
| `!freq`, `!mode`, `!width` | C and T | Not muted; joined eligible room; noob requires an online elmer; RX subrooms permit only LO-safe mapped frequency tuning |
| GPS stream subscription | Authenticated only | Existing GPS stream, scope/room checks; GPS format is fixed; client-originated position frames rejected |
| `!help` | C and T | Sent through chat; command-level outer gate requires T even for help |
| `/object`, `/rig LIST`, `/gps LIST` | Authenticated only | Object inventory/snapshots have no separate view/rx gate |
| `!power` | C and T | Not muted; current noob/elmer checks; joined TX-control room, scoped rig backend; finite positive watts |
| `/qrz`, `/grid` | Authenticated only | Configured lookup service and validated input |
| `/rig SUBSCRIBE/UNSUBSCRIBE`, `/gps SUBSCRIBE/UNSUBSCRIBE` | Authenticated only | Snapshot subscription includes generic object property updates; no separate view/rx gate |
| UUID property SET | T | Writable schema, validated value, not muted, noob/elmer, matching rig/room/VFO restrictions |
| `!vfo` | C and T | Not muted; mapped VFO in joined room; session-local selection in the issuing room; other users remain independent |

### Media

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| Binary media source injection | `media.source` or video-scoped `video-src` | Current account checked on every frame; channel subscription required |
| `/media LIST` | Authenticated only | Discovery; not permission to create or transmit |
| `/media REMOVE` (raw talk path) | A | Native/browser command UI does not currently advertise REMOVE |
| Media source registration | `media.source` or `video-src` with video-source role | Role alone grants no permission |
| `/media SUBSCRIBE` creating a channel (raw wire path) | `admin\|owner\|media.source` | View/chat-only accounts denied before shared-registry mutation |
| `/media SUBSCRIBE` existing channel | Authenticated only | Channel room and VFO mapping checked; no separate rx/tx privilege gate |
| `/media UNSUBSCRIBE` | Authenticated only | Own subscriptions |
| Ordinary TX audio frames | T | Current account, not muted, noob/elmer; authorized PTT owner/room/VFO, codec and channel subscription |
| `/rxcodec LIST`, `/txcodec LIST` | Local or authenticated discovery | No server codec mutation |
| `/rxcodec NONE`, `/txcodec NONE` | Authenticated only | Local disable/unsubscribe; no shared codec mutation |
| `/rxcodec` selection / RX picker | `rx` | Concrete channel/VFO membership, codec support and all-subscriber compatibility |
| `/txcodec` selection / TX picker | `tx` | Concrete channel/VFO membership, codec support and all-subscriber compatibility |

### Serial

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| Serial remote LIST/OPEN/CLOSE/CONFIG and byte writes | Configured access list | `serial:<port>.access`, else `serial.access`, else A; enabled export, session ownership, framing and sequence checks |

### Administration

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| `/die`, `/restart` | A | Reason required; current account, not cached staff flag |
| `/kick` | A | Target and reason required; no owner-target protection |
| `/mute`, `/unmute` | A | Target required; no owner-target protection |
| `/quota` LIST, SHOW, ADD, SET, RESET, HELP | A | Even reading quota/help requires A |
| `/rehash` | A | Direct `msg.type=rehash` checks account too |
| `/syslog on/off` | A | Subscription flag records opt-in only; each streamed log checks current account |
| `/user ADD` | A | Only owner can grant admin/owner; default new privileges are `view,chat` |
| `/user` LIST, HELP, OLDPW | A | Reads account list/password age metadata |
| `/user PASS` another account, RESETPW | A | Owner required for another elevated target |
| `/user PASS` own account | Authenticated only | 8–128 character password; clears temporary-password restrictions |
| `/user PRIVS … ADD/REMOVE/SET` | A | Owner required to edit elevated targets or grant elevated privileges |
| `/user PRIVS … LIST` | A | Can inspect elevated targets |
| `/user REMOVE`, LOCK, UNLOCK | A | Owner required for admin/owner targets; cannot remove/lock self |

Codec selection intentionally requires explicit `rx` or `tx`: admin/owner alone
is insufficient. Read-only listening and discovery require no additional flag;
room/VFO membership still applies. Flags belong to accounts, not channels, and
govern PTT/control changes. Initial negotiation of an empty channel remains
part of subscribing. `/whois` publishes known account/session metadata but never
credentials, password verifiers, authentication nonces or session tokens. These
metadata and listening policies were confirmed by the operator on 2026-10-06.

## Local frontend commands

| Commands | Account privilege required by command itself |
| --- | --- |
| Native `/admin`, `/config`, `/clear`, `/log`, `/win`, `/help` | None; UI navigation/display only |
| Native `/server`, `/disconnect`, `/quit` | None; own client lifecycle; connecting still needs server authentication |
| Native `/reload`, `/save`, `/set`, `/rxvol` | None; own configuration/volume |
| Native `/query` | None to open tab; sending uses C |
| Native `/sercom ATTACH/DISCONNECT` | None for local device/PTY management; remote export operations use server access policy |
| Native `/raw` | None to send wire data; server still enforces action policy |
| Browser `/cfg`, `/config`, `/clear`, `/clearlog`, `/clxfr`, `/chat`, `/query`, `/log`, `/menu`, `/reloadcss`, `/help`, `/rxvol`, `/rxmute`, `/rxunmute` | None for local operation; server operations still use their respective gates |
| Browser `/logout`, `/quit` | Own session only |

Native admin-marked commands additionally check the client's copy of account
privileges. That UI gate is not authoritative; server checks protect raw JSON.
Account reloads publish changed userinfo; native and browser clients refresh their
own cached privilege hints from that update. Server authorization remains authoritative.

## Fixed in this audit

- Reproduced live: logged-in VIEWER could request OWNER login and execute an
  owner-only operation without completing password authentication. Further
  login/pass commands on authenticated sessions are now rejected.
- Reproduced live: demoted administrator could still mute using cached STAFF.
  Shutdown/restart/kick/mute/unmute/syslog and log recipients now check current
  account permissions. Noob/elmer and media-source checks likewise use accounts.
- Reproduced live: view/chat account could create a shared channel for an
  unconfigured rig/VFO. Creation now requires A or `media.source`.
- Reproduced live: nonmember room posts were persisted and delivered to members.
  Room posts now require membership; replay and explicit room rosters are also
  guarded. Recipient broadcast filtering is regression-tested.
- Permission-denied/reason-required replies treated a timestamp as a `%s`
  argument and crashed the test server. Corrected those format arguments and
  the similar missing-kick-target response.
- `is_elmer_online` could loop forever on a nonauthenticated connection at the
  head of the client list. Iteration now advances and checks account privileges.
- Removed full slash-command logging in the native client and raw textframe
  logging in the server, which could include `/user pass` passwords/tokens.

## Additional fixes in this hardening pass

- Browser text is escaped before linkification for public/private/action/replayed
  chat, notices, account data and user menus. Link attributes and inline menu
  arguments are escaped independently. Added inert HTML regression cases.
- Fresh databases provision a random initial administrator password in an
  exclusive `0600` file beside the database. First login requires changing it.
  Existing database credentials are not reset. Shipped guest/static examples are
  disabled with unusable verifier placeholders; accidental enabling alone cannot
  activate a known password.
- Authentication nonces use the platform cryptographic random generator;
  challenges are renewed, single-use and bound to account/session token.
  A bounded 128-peer budget permits 20 auth messages per minute per peer,
  counting challenge requests and replies across reconnects.
- Password expiry is enforced. Temporary passwords restrict text and binary
  operations until the authenticated user changes their own password through
  `/user pass <user> <password>`; account administration remains A.
- Room creation allows all authenticated accounts for undashed names. Dashed
  names and the configured site lobby are reserved; restoration, removal and
  VFO mapping changes remain A.
- `!power` uses the common scoped control checks and the requested rig backend;
  nonfinite, malformed and nonpositive watts are rejected. `!vfo` selection is
  session-local, with acknowledgements addressed only to that session.
- Mute blocks public/private/action/file messages and topic writes and releases
  every keyed session for the account, preserving the owning rig scope.
- Incoming server-originated RX frames are rejected after explicitly authorized
  source paths. Account reloads reconcile disabled/deleted/renamed accounts,
  preserve matching session/mute state, publish privilege changes and dekey
  sessions that lost TX authority. Static password files validate UID bounds.
- Event, WebSocket and configuration diagnostics log metadata rather than
  credential-bearing payloads. Verbose live regression checks exclude test
  passwords from server logs. Guarded syslog callback recursion exposed by
  current-account checks; removed freed-value reads in configuration reload
  diagnostics. Disconnect messages drain before closing; draining sessions
  cannot submit additional requests.
- Native/browser help groups and sorts commands by `help_section`, keeping media
  commands together. Removed advertised unsupported native `/notice` and browser
  `/ban`/`/edit`; browser `/topic` and self-password changes now have usable paths.

## Remaining release concerns

| Priority | Concern | Follow-up |
| --- | --- | --- |
| High | Password verifiers remain unsalted SHA-1 and the challenge protocol is not a PAKE. A stolen verifier remains credential-equivalent. | Negotiated password/verifier migration was explicitly deferred to preserve wire compatibility. Use TLS for authentication and password changes. |
| Medium | First-run random provisioning applies to the shipped SQLite placeholder. Existing installations or custom/static account stores can still contain known credentials; old log files may already hold secrets. | Review deployed account stores and rotate exposed credentials before public release. |
| Medium | Authentication limits are process-local and peer-based; reconnects are covered, distributed attacks and broader HTTP/resource exhaustion are not fully assessed. | Review deployment limits; shared-NAT users share the peer budget. |
| Medium | Backend-specific hardware power limits and unsupported power implementations remain backend responsibilities. | Verify limits/capabilities for each supported radio before enabling remote TX. |

This is a source and targeted local-runtime audit, not an exhaustive security
assessment of HTTP/TLS, dependencies, every media decoder, or deployed settings.
Live production database/configuration were not changed.

## Implementation sources

- Native command registry: `rrclient/cmd.c:client_cmds`.
- Browser command parser: `www/js/webui.chat.js:ChatBox`.
- Authentication dispatcher: `librrprotocol/srv.http.c:ws_txtframe_process`.
- Account matching: `librrprotocol/srv.auth.c:has_priv` and `match_priv`.
- Chat/admin/room policy: `librrprotocol/srv.chat.c` and `rrserver/events.c`.
- CAT policy: `librrprotocol/srv.rigctl.c:ws_handle_rigctl_msg`.
- UUID SET policy: `rrserver/objects.c`.
- Channel/codec/source policy: `librrprotocol/ws.mediachan.c`.
- Serial policy: `rrserver/serial.c:allowed`.
- Binary frame policy: `librrprotocol/cli.main.c:ws_binframe_process_mg`.
- Log fan-out: `rrserver/hostlog.c`.
