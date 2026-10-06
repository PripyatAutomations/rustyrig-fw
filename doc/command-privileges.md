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

| Command/action | Account privileges enforced | Additional requirements / observations |
| --- | --- | --- |
| `/die`, `/restart` | A | Reason required; current account, not cached staff flag |
| `/kick` | A | Target and reason required; no owner-target protection |
| `/mute`, `/unmute` | A | Target required; no owner-target protection |
| `/syslog on/off` | A | Subscription flag records opt-in only; each streamed log checks current account |
| `/rehash` | A | Direct `msg.type=rehash` checks account too |
| `/quota` LIST, SHOW, ADD, SET, RESET, HELP | A | Even reading quota/help requires A |
| `/user` LIST, HELP, OLDPW | A | Reads account list/password age metadata |
| `/user ADD` | A | Only owner can grant admin/owner; default new privileges are `view,chat` |
| `/user REMOVE`, LOCK, UNLOCK | A | Owner required for admin/owner targets; cannot remove/lock self |
| `/user PRIVS … LIST` | A | Can inspect elevated targets |
| `/user PRIVS … ADD/REMOVE/SET` | A | Owner required to edit elevated targets or grant elevated privileges |
| `/user PASS`, RESETPW | A | Owner required for another elevated target; admin may change own password |
| `/room LIST`, `/list` | Authenticated only | Lists active room names; no membership requirement to discover names |
| `/room … VFO LIST` | Authenticated only | Reads active room bindings; no membership requirement |
| `/room ADD` / restore | A | Base rig rooms are server-owned |
| `/room REMOVE`, `-f`, `-f -h` | A | Session/room/options-bound confirmation; site and configured base rig rooms protected |
| `/room … VFO ADD/REMOVE` | A | Configured base-room bindings protected; added VFO must belong to room's rig |
| `/join`, native `/j` | Authenticated only for existing rooms | Unknown-room creation requires A; deleted rooms cannot be joined |
| `/part` | Authenticated only | Affects own membership/subscriptions |
| Public text, `/me`, room file chunks | C | Sender must belong to destination room; recipients are joined sessions only |
| `/msg`, messages from `/query` tabs | C | Private target must be a username; private messages go to endpoints only |
| `/notice` (native) | C at message entry | Sends unsupported `msg_type=notice`; no implemented delivery branch |
| `/topic` read | Authenticated only | Must belong to room |
| `/topic` write | C | Must belong to room; empty text is a query, not topic clearing |
| `/names` | Authenticated only | Roster is scoped to recipient's joined rooms |
| `/whois` | Authenticated only | Any online user; includes email, privileges, user agent and session metadata |
| `/qrz`, `/grid` | Authenticated only | Configured lookup service and validated input |
| `/object`, `/rig LIST`, `/gps LIST` | Authenticated only | Object inventory/snapshots have no separate view/rx gate |
| `/rig SUBSCRIBE/UNSUBSCRIBE`, `/gps SUBSCRIBE/UNSUBSCRIBE` | Authenticated only | Snapshot subscription includes generic object property updates; no separate view/rx gate |
| `/media LIST` | Authenticated only | Discovery; not permission to create or transmit |
| `/media SUBSCRIBE` existing channel | Authenticated only | Channel room and VFO mapping checked; no separate rx/tx privilege gate |
| `/media SUBSCRIBE` creating a channel (raw wire path) | `admin|owner|media.source` | View/chat-only accounts denied before shared-registry mutation |
| `/media UNSUBSCRIBE` | Authenticated only | Own subscriptions |
| `/media REMOVE` (raw talk path) | A | Native/browser command UI does not currently advertise REMOVE |
| `/rxcodec` selection / RX picker | `rx` | Concrete channel/VFO membership, codec support and all-subscriber compatibility |
| `/txcodec` selection / TX picker | `tx` | Concrete channel/VFO membership, codec support and all-subscriber compatibility |
| `/rxcodec LIST`, `/txcodec LIST` | Local or authenticated discovery | No server codec mutation |
| `/rxcodec NONE`, `/txcodec NONE` | Authenticated only | Local disable/unsubscribe; no shared codec mutation |
| Serial remote LIST/OPEN/CLOSE/CONFIG and byte writes | Configured access list | `serial:<port>.access`, else `serial.access`, else A; enabled export, session ownership, framing and sequence checks |
| GPS stream subscription | Authenticated only | Existing GPS stream, scope/room checks; GPS format is fixed; client-originated position frames rejected |
| `!help` | C and T | Sent through chat; command-level outer gate requires T even for help |
| `!freq`, `!mode`, `!width` | C and T | Not muted; joined eligible room; noob requires an online elmer; RX subrooms permit only LO-safe mapped frequency tuning |
| `!power` | C and T | Not muted; joined TX-control room; **remaining scope/noob issues below** |
| `!vfo` | C and T | Not muted; mapped VFO in joined room; updates shared active VFO |
| CAT frequency/mode/width (GTK, browser, raw wire) | T | Not muted; current noob/elmer checks; room/VFO restrictions |
| CAT PTT on/off | T | Not muted; TX room membership; noob/elmer and cooldown checks; ownership/preemption, quota and station interlocks |
| CAT PTT halt for another user | T plus ownership exception | Admin/owner may halt others; elmer may halt a noob; handler still enters through T gate |
| UUID property SET | T | Writable schema, validated value, not muted, noob/elmer, matching rig/room/VFO restrictions |
| Media source registration | `media.source` or `video-src` with video-source role | Role alone grants no permission |
| Binary media source injection | `media.source` or video-scoped `video-src` | Current account checked on every frame; channel subscription required |
| Ordinary TX audio frames | T | Current account, not muted, noob/elmer; authorized PTT owner/room/VFO, codec and channel subscription |
| Login / password response | Credentials for enabled account | Logged-in sessions cannot restart authentication; reconnect to change accounts; logout remains available |

Codec selection intentionally requires explicit `rx` or `tx`: admin/owner alone
is insufficient. Initial negotiation of an empty channel during subscription is
still part of subscribing, not the explicit codec-switch operation. This is a
remaining policy decision if all receive/transmit subscriptions should require
those direction privileges too.

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
| Browser `/ban`, `/edit` | Parser recognizes them; server rejects them as unknown commands |

Native admin-marked commands additionally check the client's copy of account
privileges. That UI gate is not authoritative; server checks protect raw JSON.
Existing clients cache their own privileges from login, so newly granted
privileges may need reconnect/UI refresh even though server authorization updates
immediately.

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

## Remaining release concerns (not fixed here)

| Priority | Concern and evidence | Proposed follow-up |
| --- | --- | --- |
| High | Chat HTML injection/stored XSS: `rrserver/events.c:rrserver_handle_talkmsg` forwards/stores raw text; `www/js/webui.chat.js:msg_create_links` only inserts links, and `ChatBox.Append` inserts HTML. Notices also enter HTML without escaping. | Escape text at the browser rendering boundary before linkification; preserve plain wire text; test live/replayed/private/action messages and notices with inert HTML payloads. |
| High | Predictable enabled admin credential in `sql/sqlite.master.preload.sql`, loaded for a new database. Passwords use unsalted SHA-1 in `librrprotocol/auth.hash.c`. | Replace bootstrap credential with first-run provisioning; plan versioned password/verifier migration and rate limiting. |
| High | `!power` checks selected room but emits `rigctl` without `rigctl.room`, so `rrserver/events.c` falls back to default rig. A user joined only to rig1 can address rig0's power. This path also skips the CAT handler's current noob/elmer check. | Route power through the same scoped control API; add tests for nondefault rigs, noobs and finite/range-checked power. |
| Medium | Plain public/private chat and topic writes do not check `is_muted`; mute blocks rig controls but does not implement the advertised chat ban. Muting keys down only the first matching session. | Decide mute semantics; enforce account mute on all chat paths and key down every session holding PTT. |
| Medium | `password_expires` and `password_change_required` are advertised at login but not enforced. Ordinary users cannot change their own password through `/user PASS` because its outer gate requires A. | Add authenticated self-password change and enforce temporary-password restrictions/expiry. |
| Medium | Incoming RX-direction binary frames are dispatched by `librrprotocol/cli.main.c:ws_binframe_process_mg` after authentication without per-subsystem authority checks. Impact depends on registered consumers. | Reject server-originated directions at the server boundary and allow only explicitly authorized client frame types. |
| Medium | `/whois` exposes email and detailed session metadata to every authenticated account. Generic object state and existing media subscriptions do not require `view`/`rx`/`tx`. | Review the table and decide metadata/receive permissions. |
| Medium | Disabled/deleted accounts loaded through external DB edits plus rehash do not automatically disconnect existing sessions; `/user LOCK/REMOVE` does disconnect. | Reconcile active sessions after any account reload; audit privilege revocation during active PTT/source feeds. |
| Medium | `librustyaxe/event-bus.c:event_emit` still logs serialized event payloads at CRAZY level; `user.cmd` can contain a password. Direct command/frame logs were removed, but this remaining trace path also needs redaction. | Redact sensitive event fields or log metadata only; test verbose logs for absence of credentials. |
| Medium | No login throttling found in the owned auth/dispatcher paths; unauthenticated hello/pong remain accepted. Not a complete network DoS review. | Add bounded per-peer/auth attempt limits and verify connection/time/size limits. |
| Low | Browser `/ban` and `/edit`, native `/notice`, and ordinary self-password changes lack usable server implementations despite partial UI paths. | Remove unsupported help/commands or implement documented behavior. |

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
