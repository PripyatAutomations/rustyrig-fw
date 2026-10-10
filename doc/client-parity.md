# RustyRig C / WebUI parity map

The native C client and browser WebUI are separate implementations of
the RustyRig client. They do not need identical source code, but they
must have equivalent observable behavior for concepts marked below.

The WebUI is in the separate repository:
`PripyatAutomations/rustyrig-www`

## Current likely parity areas

| Concept | Native C | WebUI JS | Parity |
|---|---|---|---|
| Compact wire boundary | `librrprotocol/wire.c`, `wire-schema.json` | `js/webui.wire.js`, generated registry | Yes: rustyrig.v1 only, strict decode and bounded sends |
| Connection management | `rrclient/connman.c` | `js/webui.js` and connection-related code | Yes |
| UUID object/property cache | `rrclient/objects.c`, `objects.events.c` | `js/webui.objects.js` | Yes: discovery, ownership, versions, lifecycle; diagnostic UI differs |
| Frequency/VFO behavior | `rrclient/vfo.c` | `js/webui.frequency.js` | Yes |
| CAT/radio control semantics | `rrclient/cat*.c` | `js/webui.rigctl.js` / related code | Yes where exposed by WebUI |
| Chat behavior | `rrclient/chat*.c`, `rrclient/m_privmsg.c` | `js/webui.chat.js`, `js/webui.chat.completion.js` | Yes |
| Input/command behavior | `rrclient/cmd*.c` | `js/webui.input.js`, `js/webui.js` | Yes where commands are exposed |
| Authentication | client connection/auth behavior | `js/webui.auth.js` | Yes where applicable |
| Notifications | native event/UI handling | `js/webui.notifications.js` | Equivalent user-visible events |
| System log | native syslog/event handling | `js/webui.syslog.js` | Equivalent user-visible behavior |
| Window/UI management | GTK/TUI-specific | `js/webui.winman.js` | No source parity; UX may differ |
| Audio implementation | `rrclient/audio.c` | `js/webui.audio.js`, `js/webui.audio.framing.js` | Protocol/observable behavior where shared |
| File transfer | native implementation if present | `js/webui.filexfer.js` | Yes where native client supports it |

This map is deliberately conservative. Verify the actual implementation
before treating a row as a one-to-one mapping.

SOCKS5 connection settings are native-only transport configuration. Browsers
manage proxies outside the WebUI; no JavaScript SOCKS handshake is mirrored.

## Deferred media parity

The native client now provides `/rxcodec` and `/txcodec` in GTK and TUI,
plus GTK `NONE` selections that unsubscribe audio channels. UUID-specific
selection uses the existing protocol. These controls and G.722 playback are
not yet mirrored in the outdated WebUI media implementation. The browser's
`js/webui.audio.js` implements PCM and mu-law; adding GStreamer pipelines to
the native configuration does not add browser decoders. See
[Native audio codec selection](media-codecs.md) for current native semantics
and the one-local-pipeline-per-direction limitation.

Ogg/Vorbis (`oggv`) is also native-only; browser decoder support remains
deferred. Shared `/media`, user-targeting, `/quota` and `/syslog` parameter
completion is mirrored in `js/webui.chat.completion.js` and native
`rrclient/cmd.completion.c`. Native `/rxcodec` and `/txcodec` completion follows
the existing native-only media controls. GTK and TUI share the same provider.
The `pc1T`, `g72T`, `mu1T`, `mu0T`, `opuT`, `oggT`, `aacT`, `flaT`, `pc1P`,
`g72P`, `mu1P`, `mu0P`, `opuP`, `oggP`, `aacP` and `flaP` test IDs are native
pipeline variants; they are not added to browser codec advertisement.

## What parity means

Parity means equivalent externally observable behavior, not identical
implementation.

For a shared feature, keep synchronized:

- protocol messages
- command names and semantics
- state transitions
- validation/range checking
- frequency representation and operations
- event handling
- user-visible errors
- user-visible state
- connection/disconnection behavior

Language/framework differences are expected.

## What does NOT need parity

These are normally frontend-specific:

- GTK widgets
- TUI terminal layout
- DOM manipulation
- CSS
- browser-only APIs
- keyboard/mouse/touch presentation
- browser-specific audio plumbing

Do not modify one merely to make its implementation resemble the other.

## Parity workflow

When changing a shared feature:

1. Identify the authoritative protocol/server behavior.
2. Find the C implementation.
3. Find the JS implementation.
4. Decide whether the behavior is truly shared.
5. Change both when required.
6. Test both when practical.
7. If intentionally diverging, document why.
8. If unsure, pause and ask before proceeding

## Search markers

Use:

    PARITY:

to identify coupled implementations.

Example:

    /* PARITY: rustyrig-www/js/webui.frequency.js */

and in JS:

    // PARITY: rustyrig-fw/rrclient/vfo.c

The configurable `tui.status-line` top/topic row is frontend-specific. It
reads the native client's existing VFO state; it introduces no protocol or
CAT semantics. GTK widgets and browser DOM displays retain their own layout.
See [TUI top status line](tui-status-line.md).

## Rig-room audio

Native `rrclient/media.c` and browser `js/webui.media.js` honor session-specific
`media.room`/`media.joined` metadata. Both attach the active VFO pair only in a
joined rig room, switch automatic subscriptions when selecting another joined
rig room, and drop room audio on PART. Login joins the site lobby; optional
autojoin uses the native server profile or the browser's site settings. See
[Site lobby and rig rooms](rig-rooms.md) for configuration and room-scoped CAT/PTT authorization.
Both clients select audio by room and VFO, recognize the sole TX base room,
and permit RX-subroom frequency edits only for the advertised per-VFO tuning
mask. GTK docked row selection, detached panels, and CSS highlighting are
frontend-specific; browser frequency controls honor the same tuning policy.

## Local serial services

`/sercom` and local PTY/real-device transports belong to the native common
client and work in GTK and TUI. The browser recognizes the command and explains
this limitation; it does not forward local endpoint management to the server.
Both command completers offer LIST, REMOTE, ATTACH, and DISCONNECT.
Native `/sercom [list]` shows local attachments and requests permitted server
ports; the browser shows permitted server ports only. Server serial export
listings span the connection, independent of the current rig tab, and exclude
ports reserved for server-local GPS services. Native CAT writes
use the same room-scoped protocol APIs as the other native controls. See
[Serial endpoints and rig GPS](serial-interfaces.md).

Server real-port passthrough uses MODEM/`seri` binary frames and native local
PTYs. GPS uses separate read-only MODEM/`gpsp` media channels carrying compact
per-rig position snapshots and station fallback. Both C and browser clients
synthesize matching RMC sentences from the integer coordinates; native
`rig.gps-out` endpoints follow the active rig and automatically subscribe, while
browser subscriptions are explicit and emit `rustyrig:gps-nmea` for integrations.
The browser cannot create local PTYs. Server coordinates and receiver selection
are authoritative; see the `gpsp`/RMC parity marker in both client implementations.

## Resource discovery

Native GTK/TUI and WebUI share `/rig list`, `/rig subscribe|unsubscribe`
(UUID property updates), and `/gps list|subscribe|unsubscribe <scope>`.
The server supplies the resource tree and permission-filtered serial exports.
Clients filter `/rig [list]` to rigs and VFOs, `/gps [list]` to GPS services,
and browser `/sercom [list|remote]` to serial exports. Type filters preserve
ancestor context even when ancestor rows are hidden.
`/sercom remote` discovers server exports in both clients; only the native
client can attach local PTYs or serial devices. See [Resource discovery](resource-discovery.md).

## Chat input history

GTK and TUI default to shared command/chat history across windows via
`ui.shared-input-history=true`; false keeps history per window. The browser
also shares input history. GTK Up/Down operates on the originating entry,
restores that entry's unfinished draft after the newest item, and retains up
to 50 entries. GTK and browser suppress consecutive duplicate submissions.

Explicit native `rig.nmea-out`, `rigN.nmea-out` and `station.nmea-out` ports
subscribe to separate MODEM/`nmea` receiver streams. Browser integrations may
subscribe explicitly through `/media`; both clients validate complete sentences
and route them through their existing GPS output events. Default `gps-out`
ports continue using compact `gpsp` records and local RMC synthesis.

## Human resource navigation

Native GTK/TUI and browser `/media subscribe|unsubscribe` resolve unique,
case-insensitive stream names locally and send UUIDs. Media and codec listings
show names and room/subscription metadata; completion inserts names and displays
separate descriptive labels. Ambiguous/unknown names are rejected. `/object`
shows readable cached object/property data and accepts UUIDs or qualified
symbols such as `rig0` and `rig0.A`. These conveniences do not change wire
addressing or the site-wide `/rig subscribe` semantics.

Resource command replies use the issuing window in C and JS. Inventory requests
retain that window across tab switches. `/rig`, `/gps`, `/object`, `/media` and
codec listings/completion filter by site or rig room (including RX rooms);
status/no-room contexts see all resources. Explicit names/UUIDs remain usable
outside those default listings. See `rrclient/resource.context.h` and
`rustyrig-www/js/webui.media.js` (`mediaResourceMatches`).

## Rig command chains and GTK selectors

Rig chat commands accept chains such as `!mode lsb freq 7200` and
`!freq 7200 mode usb`; the shared server parser is authoritative for both
clients. Successful controls publish backend observations promptly, so
UUID-backed controls and user lists converge without waiting for a periodic
poll. GTK applies mode/width observations with edit handlers blocked.

GTK mode selectors cycle through matching first letters, including while the
dropdown is open. Width selectors use A for NARR, N for NORM, and W for WIDE.
These keyboard bindings are GTK-specific and preserve modified global shortcuts.
Native `/help` documents these bindings; native and browser help include a
command-chain example alongside the current discovery command names.

## Room management

Native `rrclient/cmd.chat.c:cmd_room` and browser `js/webui.chat.js` forward
`/room` arguments to the authoritative `librrprotocol/srv.chat.c` parser and
`rrserver/events.c` policy handlers. Both clients support action-first add/remove,
legacy room-first commands, confirmation tokens and `--force/-f --history/-h`.
Help and completion describe the same commands. Server policy reserves dashed station room creation, restoration and removal
for admin/owner; undashed room creation is available to any authenticated account.
It confirms every removal; database auditing belongs to rrserver.

## Account authorization

C server policy is authoritative for both clients; see [command privileges](command-privileges.md).
Shared codec changes require explicit account `rx`/`tx` matching the concrete
channel direction, with existing room/VFO membership checks. Admin/owner commands
use current account privileges rather than cached connection flags. UI permissions
are hints only; raw JSON is subject to the same server checks.

## Help and security rendering

Native `client_cmd_t.help_section` and browser `webui_command_help.help_section`
group and sort `/help` into Connection, Chat and rooms, Radio and discovery,
Media, Serial, Client settings and Administration. Frontend-only commands differ.
Staff commands are hidden according to exact account tokens; `/user PASS` for
one's own account is available to everyone. Both clients consume current-account
userinfo updates. `!vfo` acknowledgements apply to the issuing session and room.
Browser chat and notice rendering escapes plain text before producing DOM HTML;
URLs remain clickable without interpreting message text as markup.

Fresh SQLite databases place the random first-login administrator password in
`<database-path>.bootstrap-password` with mode `0600`. Login with it and run
`/user pass admin <new-password>`, then remove the credential file securely.
Existing databases and custom preloads are not reprovisioned. Authentication
wire messages and password hashing remain compatible; migrated storage is deferred.

Read-only listening does not require an RX flag. Account flags govern PTT, rig
controls and explicit codec changes; channels do not carry authorization flags.
Whois metadata is public to authenticated accounts, excluding authentication
secrets. Room membership remains required for room chat and VFO media.

## Transport input validation

C `librrprotocol/binframe.c:rr_binframe_parse` and browser
`js/webui.binframe.js:binframe_parse` require exact header/payload lengths.
Both reject truncated and trailing data. Server JSON/CAT/serial validation is
authoritative for native and browser requests; named width presets and numeric
Hz labels remain valid. PTT release permits the holder or a strictly higher account role: owner above
admin, admin above ordinary roles, TX/elmer above noob. Overrides only stop TX;
key-down never transfers ownership. GTK/browser clicks on another holder send
stop requests. A keyed session must release before switching rig/VFO. Serial IDs are never recycled
within a connection. See [transport security audit](transport-security-audit.md)
for verified fixes, LAN/VPN assumptions and remaining findings.

Raw serial discovery and tunnel access are server-authoritative for both clients.
Account flags `serial`, exact `serial.<portname>` or a trailing prefix pattern
(e.g. `serial.ttyGPS*`) are required; admin/owner alone does not grant access.
Read-only GPS media retains its room membership policy.

GTK VFO rows and browser VFO selectors explain that VFO audio routes may
share a receiver/transmitter. Server media descriptions identify routes;
per-VFO subscriptions remain available for independent RX/TX rigs.

Shared rig audio is authoritative in `rrserver/media.c`: `audio.per-vfo=false`
by default, with independent RX/TX overrides. C `rrclient/media.c` and browser
`js/webui.media.js` select VFO_NA channels, retain them across VFO changes, and
release them when switching rigs. Shared audio uses rig-owned VFO objects for
control displays. The wire uses the existing VFO_NA value and unchanged room,
codec and PTT ownership checks.

GTK keeps a persistent status tab separate from the authenticated site lobby.
Its command input uses the unscoped discovery context, matching TUI status;
NULL/status output goes to its own buffer while room/query output remains scoped.
Browser chat retains its existing root/status discovery context.

GTK-only interface zoom: Alt/Ctrl +/- (Alt/Ctrl = and keypad +/-) changes
`ui.gtk.zoom` in 10% steps from 25% to 300%. Point-font DPI, CSS pixel sizes,
widget requests, margins, spacing, packing and icon sizes scale together.
New dialogs/detached windows inherit the current zoom; CSS reload preserves it.
TUI text sizing remains controlled by the terminal, and browser zoom by the browser.

VFO selection is per client session and room. `!vfo` returns
`cat.state.selected=true` only to its caller; physical `cat.state.active`
poll reports never select the client UI VFO. The acknowledgement retains
`active=true` for older clients. Native and browser clients require the updated
server for selection acknowledgements. GTK userlist rows target their own room.
Hamlib selects the requested hardware VFO before key-down and avoids polling
other VFOs while transmitting. Key-up never changes hardware VFO selection.
Protocol error messages contain plain text; browser rendering escapes it.
Alt/Ctrl 0 (including keypad 0) resets GTK zoom to 100%.

GTK zoom also rescales the GTK theme's pixel dimensions (including control
minimum sizes, padding, combo arrows and slider nodes). This allows the actual
frequency entry and VFO row to shrink, rather than only their text. The scaled
theme stays below application/user CSS; 100% removes that override. This is a
GTK presentation detail, with no protocol or browser behavior change.

`ui.gtk.scale-on-resize=true` (default) scales the main window against its
monitor's usable work area, using the smaller width/height ratio, from 25% to
100%. Maximized/fullscreen is 100%; secondary windows inherit the scale but do
not drive it. A scrollable root lets the main window shrink below its contents'
previous minimum; overflow remains reachable at the 25% floor. Resize updates
are debounced and never resize the window themselves. Ctrl +/- changes font/UI
zoom and proportionally resizes the window; Alt +/- changes font/UI zoom only.
Reset to 100% maximizes the window in automatic mode. Set the option false for
manual 25–300% zoom. Saved window placement still applies;
without saved placement, automatic mode opens at 75% of the primary monitor.

GTK multitouch: two-finger pinch changes font/UI zoom only in 5% steps within
the current zoom limits. Window edge resizing (with automatic scaling enabled)
continues to update the zoom. A stationary two-finger touchscreen
tap on a userlist row opens its existing right-click menu after both fingers
lift (400ms tap limit, 10 logical-pixel movement tolerance). Pinches, drags,
extra fingers and cancelled touches do not open menus. Mouse and touchpad
right-click remain supported. Pinch/tap gestures are GTK-only and leave
browser-native gestures unchanged.

The GTK main notebook tab strip wraps tabs to additional rows instead of using
horizontal scroll arrows, so every tab remains directly visible and selectable.

Built-in native client messages now use mIRC color/style controls. GTK already
renders these; TUI rendering reuses `irc_to_tui_colors`, including style toggles
and numeric text after two-digit color codes. Chat input in GTK and TUI supports
Ctrl+B/C/I/O/R to insert bold, color, italic, reset and reverse controls. Ctrl+U
clears the current input. Theme and status-line colors use the same IRC controls
and config escapes documented in `doc/irc-formatting.md`. Help section headings
are red in both clients.
PTT/control errors identify the VFO and room; codec errors identify the channel
and codec where known. Privilege rules and wire command semantics are unchanged.

## Server URL transports

Native GTK/TUI profiles require a complete `server.url`; ports are optional:
`ws://host:port/path`, `wss://host:port/path`, `irc://host:port`, or
`ircs://host:port`. Defaults are WS 8420, WSS 4420, IRC 6667 and IRCS 6697.
The shared native connection manager selects WebSocket,
WebSocket with TLS, IRC, or IRC with TLS. IRC message events are JSON and
native application listeners translate them into shared chat/room events.
IRC provides conventional chat; RustyRig JSON control/media commands require
WebSocket. See [IRC and server URLs](irc-client.md).

IRC transport is native-only. The WebUI does not need to implement, track or
mirror IRC behavior, now or in future changes. Do not make WebUI changes for
IRC parity. The browser retains its existing WebSocket connection behavior.

Native simultaneous connections, status-tab server selection and per-server
conversation tabs are frontend-specific for this change. The browser keeps
its existing connection presentation; IRC remains outside browser parity.
