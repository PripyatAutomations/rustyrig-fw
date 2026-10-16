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
and per-channel RX playback behavior.

Ogg/Vorbis (`oggv`) is also native-only; browser decoder support remains
deferred. Shared `/media`, user-targeting, `/quota` and `/syslog` parameter
completion is mirrored in `js/webui.chat.completion.js` and native
`rrclient/cmd.completion.c`. Native `/rxcodec` and `/txcodec` completion follows
the existing native-only media controls. GTK and TUI share the same provider.
The `pc1T`, `g72T`, `mu1T`, `mu0T`, `opuT`, `oggT`, `aacT`, `flaT`, `pc1P`,
`g72P`, `mu1P`, `mu0P`, `opuP`, `oggP`, `aacP` and `flaP` test IDs are native
pipeline variants; they are not added to browser codec advertisement.

Native speaker subprocess recovery is implementation-specific: after a failed
PCM write or exited playback worker, rrclient recreates its speaker endpoint
with bounded retries and restores the configured RX volume. Browser playback
uses WebAudio and has no corresponding subprocess to restart.

L/R/S/M routing and `/rxroute` have equivalent native/browser behavior:
routing is independent of UI selection, defaults to both ears, and mute keeps
subscriptions and decoders alive. Settings are per server/room for the current
session. Native RX uses a bounded integer stereo mixer; WebAudio uses gains and
panners. Native and browser reconnects start at one second and double to sixty,
reset on authorization, and retain the ten-attempt limit. The former default
interval of sixty seconds is treated as a legacy default and becomes one.

The RX Audio Routing floating window is GTK-specific presentation of the
native stream mixer. Its Split RX and per-ear stream selectors operate on
existing subscriptions across native server connections, leave decoding alive
when muted, and do not add protocol messages. `/rxroute` remains available
in native/browser clients, and browser room controls remain; the browser has no
floating GTK window or cross-connection stream picker. Individual native
stream choices last for the subscription and survive codec changes, while
room preferences retain their existing session/reconnect lifetime.

Receiver bridges publish configured/cached offline capability hints through
ordinary rig/VFO descriptors. Native and browser clients consume the same
coverage/mode schemas; availability remains separate. User demand and metadata
persistence belong to rrserver, so no new client wire messages are required.

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
joined rig room, retain those subscriptions when selecting another joined
rig room or VFO, and drop room audio on PART. Inactive RX channels continue
receiving and decoding with locally muted output; selecting them again only
changes the output gate. Each channel keeps independent decoder state, even
when codecs match. NONE, removal, disconnect and codec replacement still
release the affected decoder. Login joins the site lobby; optional
autojoin uses the native server profile or the browser's site settings. See
[Site lobby and rig rooms](rig-rooms.md) for configuration and room-scoped CAT/PTT authorization.
Both clients select audio by room and VFO, recognize the sole TX base room,
and permit RX-subroom frequency edits only for the advertised per-VFO tuning
mask. GTK docked row selection, detached panels, and CSS highlighting are
frontend-specific; browser frequency controls honor the same tuning policy.
GTK's shared VFO codec pickers refresh from the selected tab's confirmed RX/TX
channels and its server's negotiated list, including NONE. Repainting them
sends no codec requests, and background-server observations update only their
own cache. The browser has no equivalent GTK codec-picker widget.


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

Native `rrclient/cmd.chat.c:cmd_room` and browser `js/webui.chat.commands.js` forward
`/room` arguments to the authoritative `librrprotocol/srv.chat.c` parser and
`rrserver/events.rooms.c` policy handlers. Both clients support action-first add/remove,
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
are red in both clients. Native IRC formatting supports indexed colors 0-98,
default 99 and hex RGB foreground/background controls, preserving independent
color/style state. GTK offers the extended palette and preserves hex controls
in input/history; TUI emits 256-color and true-color ANSI sequences. The
browser chat escapes/linkifies messages and has no IRC transport or formatter;
this native IRC rendering change does not add browser IRC behavior.
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

Native simultaneous connections, status-tab server selection, per-profile proxy
settings and conversation tabs are frontend-specific for this change. The browser
keeps its existing connection presentation; IRC remains outside browser parity.

## Media adaptation and usage accounting

Native and browser clients share compact `media.feedback` fields, relative
monotonic timing, per-stream sequence-gap detection and bounded playback recovery.
Native DSP and WebCodecs have different decoder capabilities; neither client
advertises codecs its active implementation cannot decode. The server alone owns
persistent usage, allowance decisions and owner/admin/elmer quota visibility in
`/whois`. Both clients display TX/BW status; strict accounting failures reject
PTT on while keeping PTT off available. Both clients
forward complete `/quota [TX|BW]` command tails, complete unit/action parameters,
and display staff usage when present. IRC remains native-only.

See [Media adaptation and usage accounting](media-adaptation-and-usage.md).

Native `/server` WS/WSS URLs may include `user:pass@` for connection-specific
RustyRig login credentials. This command syntax follows native profile and
multiple-connection management. The browser has no `/server` command and
continues using its page-origin WebSocket URL and authentication controls;
no browser URL userinfo or HTTP Basic authentication is introduced.

Native configuration loads `/etc/rustyrig/rrclient.cfg` followed by
`~/.config/rrclient.cfg`. The latter overrides individual values, including
credentials, and reload preserves this order. Source-tree binaries instead
use local configuration; an explicit `-f` file remains standalone.
GTK's server chooser has Edit and Alt-X opens the chooser. Editing writes only
changed profile settings to the user file, with private file permissions,
retaining other user overrides and leaving the system configuration untouched.
These native profile/editor and filesystem conventions do not change the
browser's page-origin connection or authentication controls.

GTK room rosters are owned by a server-qualified room. Detached roster close
buttons dock the roster; Alt-U restores/docks or detaches the current room's
roster. The divider may collapse it without destroying it. Saved configuration
uses `ui.userlist.{irc,vfo,room}.{width,height,dock-width,columns}` for shared
layout preferences by room type; column IDs remain stable when reordered.
These widget and window preferences are native GTK-specific.
Unset roster widths (`0`) fit all visible fields and VFO controls at first
allocation; the measured width is retained in the running configuration and
persisted by the normal save commands. Explicit widths take precedence.
An initially auto-sized GTK roster keeps growing while JOIN/userinfo messages
populate it. Dragging its divider or resizing its detached window stops that
automatic growth; measurements include the scrollbar and divider. Saved sizes
are honored on subsequent launches. Maximized window size is not saved over
the detached window's normal size.
GTK room Username columns expand into remaining roster space and ellipsize
when narrowed, leaving room for visible status fields. Restoring a saved VFO
roster enforces enough width for its headers and controls; full usernames still
determine its natural auto-sized width. Manual divider collapse remains allowed.
Docked GTK frequency/mode/codec/PTT controls have their own horizontal scroller.
Their minimum width must not enlarge the entire notebook in the main zoom
viewport and place the room roster outside the visible window.

GTK roster Query/Whois/Mute/Unmute/Kick actions select their existing room tab
and invoke the native slash-command parser directly. They leave the input
draft and history untouched; browser menu presentation remains separate.

Available rooms display/completion preserves the connected server's LIST.
Native room state, including the available-room cache, is saved separately
per server connection; selecting another server selects its own LIST. Room
names are not filtered by station prefix.

Linked-server discovery remains scoped to each client connection. Native and
browser LIST display `↗` for `talk.remote-rooms` and remote `talk.servers`, but
keep plain room names for completion. Both support `/users` with notices in the
issuing room. Owner object updates use the connection's normal stream epoch and
sequence; remote JOIN supplies the subscribed room's property schema/state.
IRC behavior remains native-only.

GTK and browser transcripts link room references in chat, topics, notices and
LIST output. Activating a reference selects an existing room tab or joins using
the containing transcript's connection. Drafts remain untouched; existing URL
links and text selection remain usable. Terminal rendering remains plain text.

The native GTK server-profile editor has one space-delimited Rooms field for
all autojoins. It merges legacy chat/rig lists when opened and writes edited
lists to the user's `autojoin` override, masking the legacy `autojoin.rig` list.
The browser uses its existing site preferences rather than native profiles.

## Direct and linked audio transport

Native GTK and TUI share `rrclient/media.direct.c`: client-initiated authenticated
RTP/UDP for RX/TX, with WebSocket fallback. Browsers continue using WebSockets;
the owning server's private channel announcements and the home server's translated
binary frames provide linked RX/TX without adding a browser UDP implementation.
Proxy TX and its media-liveness watchdog are server-authoritative for both
native and browser clients. Codec support differences described above still apply.
The generated browser wire registry includes the new fields; keys are private
subscription replies, never discovery/catalog broadcasts.

Both clients accept `/media subscribe -proxy <channel>`. Native clients also
accept `-direct`, requesting a private RTP referral and reporting unreachable
endpoints without silently switching live audio to WebSockets. Browsers explain
that direct RTP needs the native client. Reliable codec setup stays on WebSockets.

Named listener/peer/RTP ACLs are server policy and apply to all clients.
IRC DCC AUDIO referrals are native-only, like other IRC behavior; browsers
continue using WebSocket media and do not consume direct UDP referrals.

The native server editor's direct-media checkbox and `/media direct on|off|status`
select a per-profile preference; automatic subscriptions are refreshed immediately.
The native config editor exposes the inherited global default as a checkbox.
Peer DCC AUDIO v2 is microphone voice with explicit acceptance and a separate
mute setting; v1 remains owner-authorized rig audio. Peer UDP, native profile
preferences, and IRC commands have no browser equivalent.

Classic DCC CHAT/SEND are native-only IRC transports. Shared `rrclient/dcc.c`
commands require explicit incoming acceptance; `/dcc get ID [filename]` defaults
to the offered basename in `~/Downloads/DCC/`, creating that directory as needed; GTK and TUI both use `librrprotocol/irc.dcc.h`. Session notices
and chat replies retain their issuing/accepting window and server context.
There is no browser DCC counterpart.

Automatic native RTP subscriptions try the rig owner, then the connected server's
RTP endpoint for linked channels, then WebSocket proxy media. Five-second UDP
deadlines advance the selection; explicit `-direct` still requires owner RTP.
Connected-server grants use independent keys and translated stream IDs, then
proxy both directions over the owner's WebSocket link. Full proxy media also
supports both RX and TX when RTP is unavailable. Browser microphone frames in
`js/webui.audio.js:webui_append_tx_samples` already use this same WebSocket path.
The owner requires an authorized TX subscription and accepted microphone audio
within three seconds of key-down; subsequent media gaps release PTT. Account,
codec, room, VFO and PTT ownership checks remain authoritative in C. RTP
selection itself remains native-only.

## Connection recovery and administration

Native `rrclient/connman.c` and browser `js/webui.js` use a 60-second default
reconnect interval with global and per-server settings. Explicit disconnect
still cancels retry. IRC nickname conflicts and GTK prompts are native-only;
RustyRig usernames allow multiple sessions and do not use nickname fallback.

Native GTK `gtk.admin.c` and browser `js/webui.admin.js` display server-owned
SQLite inventory and paged audit/PTT logs through `talk.admin`. Both provide
database account edits, password reset, TX/BW quota management, Lock/Unlock,
room topic edits and confirmed Remove/Restore. Permanent room removal warns
that chat history will be lost.
Native TUI uses `/admin`. `/link list|open|close` has matching C/JS command
semantics and controls stored peers on the connected server. GTK and browser
PTT controls retain orange `TOT EXPIRE` until a local acknowledgement click,
which sends no command. See [Connection recovery and administration](connection-admin.md).


Native `rrclient/supervision.c` and browser `js/webui.admin.js` consume
server-authoritative `talk.supervise` events. GTK/browser show responsibility
prompts and a clickable grey REQUEST button for unsupervised noobs; TUI prints
an alert in the TX room with `/supervise accept|decline <id> <room>` guidance.
Consent is per login session and TX room; idle expiry and logout release PTT
in `rrserver/supervision.c`. IRC does not participate.

Native IRC rosters defer population redraws across 353 NAMES replies until
366 ends that room's list. Pending batches belong to each connection and are
cleared on room removal/disconnect. GTK keeps pending rosters intact during
other room redraws. IRC batching has no browser counterpart.

GTK Admin Ctrl-Tab cycles the selected server and refreshes its inventory.
While Admin is active, Ctrl-Left/Right wrap through its internal pages;
Ctrl-1 through Ctrl-9 and Ctrl-0 select Admin pages 1 through 10 when present.
Alt-Left/Right and Alt-number continue to select the main notebook tabs. These are
native GTK shortcuts; browser keyboard handling remains frontend-specific.

Native IRC chat uses each connection's retained ISUPPORT LINELEN to split
outgoing text, including /msg, /me and /notice, with per-chunk local echo.
ISUPPORT, CAP and legacy CAPAB retention and IRC splitting are native-only;
the browser continues using the RustyRig WebSocket protocol.

GTK roster presentation updates existing rows and moves them for ordering
changes, preserving selection rather than clearing the model. Unchanged
rosters produce no row notifications; width measurements are coalesced until
the next idle callback. VFO polling updates labels/styles only when displayed
values change. GTK and TUI use the same nonblocking transport sweep. GTK schedules
it at a high-priority 10 ms GLib timeout, leaving GLib to wait between sweeps rather
than blocking GTK inside Mongoose. This scheduling is native-specific; browser
WebSocket events are driven by its event loop. Shared client state and wire
behavior are unchanged. GTK coalesces radio observations and roster redraws
into idle paints after transport/audio servicing. Queued VFO paints retain their
rig/server/VFO scope and are discarded after selection changes or shutdown.
Unchanged frequency digits, mode/width rows and PTT styles do not mutate GTK
widgets. These rendering optimizations are frontend-specific; TUI and browser
protocol state and command behavior are unchanged.

Native `objects.events.c` and browser `webui.objects.js` render `/rig` as
a connection-wide site → rig → VFO tree, including linked sites. GPS inventory
retains site and rig parents and also spans the connection. Reply destinations
remain the issuing window; cached object/media context filters are unchanged.

IRC SASL is native-only. `librrprotocol/irc.capab.c` negotiates PLAIN or EXTERNAL
before CAP END; `rrclient/connman.c` supplies per-profile `server.sasl.user`,
`server.sasl.pass` and `server.sasl.mechanism`. See [IRC SASL](irc-sasl.md).
Native media notices identify direct owner RTP, server-proxy RTP or WebSocket
proxy and their endpoints. Browser media notices identify WebSocket proxy;
browsers do not implement the native RTP route selection. GTK restores a local
TX button from the current room/VFO's authoritative roster after tab switches,
without sending CAT commands. Both clients bind the main PTT presentation and
pending state to the displayed room/VFO, clearing the prior context on a switch
and restoring only that context's cached talker. GTK also isolates connections.
Browser talker rendering filters explicit PTT room/VFO metadata.

GTK Config live field filtering is frontend-specific. Plain text matches keys
and descriptions; `=text` matches only keys and `^text` matches key prefixes,
case-insensitively. See [Config field search](config-search.md).

GTK and browser Admin quota tables display separate server TX and RX bandwidth
usage totals from `rrserver/events.admin.c`, with a shared remaining allowance. Both
label pages Users, Rooms, Quotas, Audit Log and PTT Log, and show the zero-to-
exhaust hint only for SET actions.

Native `rooms.c` and browser `webui.chat.js` retain offline linked rooms using
server `talk.room-state` notifications. Ordinary chat drafts stay unsent; slash
commands remain available. GTK/browser show OFFLINE rosters and disable rig
controls, and self JOIN thaws restored rooms. TUI uses the shared input rejection
callback to preserve its draft. See [Offline room tabs](server-linking.md#offline-room-tabs).

Native/browser media connection notices use readable stream paths. Routine
native request/connecting phases are debug-only; confirmed connections and
fallback reasons remain in room chat. Codec confirmations follow the stream
room. See [Media diagnostics](media-notices.md).

Native and browser password responses echo the challenge session token alongside
the username and nonce-derived SHA-1 response (`librrprotocol/ws.auth.c` and
`rustyrig-www/js/webui.auth.js`). Media diagnostic paths use catalog endpoint
names, including GPS/NMEA, and fall back to the control-room station name until
object discovery supplies its alias. GTK codec-picker replies, like native
codec commands, are delivered to the selected channel's room.

Media catalog broadcasts follow the authenticated WebSocket recipient rules in
`librrprotocol/srv.send.c`; ordinary HTTP page and asset connections receive no
media frames. Only authenticated WebSocket disconnects refresh the catalog.
Explicit server-link catalog delivery continues through the existing catalog
event and relay handlers.

## Server backend scheduling

Radio backend I/O runs on a worker per server rig. Property control results
are sent after the hardware operation and readback complete; property updates
remain authoritative. Native and browser clients use the existing result and
observation messages without a wire change. Legacy CAT PTT receipts retain
reservation semantics; confirmed CAT state follows backend completion.
See [Backend workers](backend-workers.md) for ownership and scheduling details.


## External bridge receiver control

`rrserver/bridge.manager.c` publishes bridge receivers through the existing
UUID object/property protocol. Native `rrclient/objects.c` and browser
`js/webui.objects.js` consume the same frequency bounds, mode enums and
availability without driver-specific messages. Native and browser clients
receive bridge RX audio through the existing codec/media paths. TX media is
not provisioned for these receivers. See [External receiver bridges](rrbridge.md).

## Receiver directory and bridge access

The native GTK Receiver Directory and `/receivers` use a local SQLite cache
and explicit configurable mirror/import source. This native filesystem/dialog
integration is frontend-specific; the browser has explicit receiver URL actions
but no directory fetch/import dialog. Receiver actions extend the existing
admin envelope without adding a wire operation. Kiwi is the first provider;
other WebSDR types require their own parser and operator policy.

Read-only `receiver.*` rig properties carry learned metadata through the existing
object protocol; both clients can inspect them. Native clients additionally
merge confirmed observations into local receiver details. Directory refreshes
never overwrite live learned fields. Missing/offline records and local failures
are separate, and missing records are purged after ninety days without a listing
or live observation. Server bridge-use admission and media access apply equally
to native/browser clients, including entitlement revocation. `bridge.admin` and
`websdr.listen`, `websdr.connect` and `websdr.admin` enforce configured listening,
ad hoc temporary receiver creation and configuration persistence separately.
Native and browser `/receivers connect|add <URL>` use the same server checks.
See [Receiver Directory](receiver-directory.md).

GTK opens the Receiver Directory with Alt-Z (also documented in `/help`);
Alt-X remains the server chooser. GTK RX routing uses its separate window
rather than tab-strip selectors; browser room controls and `/rxroute` remain.
Native playback follows absolute monotonic 20 ms deadlines with bounded
100 ms catch-up after late dispatch; browser playback uses its audio clock.
The default negotiated transport preference is Opus with supported fallbacks.


GTK Resources (Status button or Alt-O), `/object`, `/rig` and `/gps` use one
resource manager with all known server connections and full site/rig/VFO/service
trees. All resources stay visible; separate columns show availability and the
user's room/media/object/serial state. Contextual actions use the originating
server and existing common handlers. Local serial attachments remain available
for inspection/disconnection while servers are offline. TUI and browser commands
retain text trees; this native resource window has no DOM/source parity
requirement. `/object <symbol|uuid>` retains cached property inspection. See
[Native Resources manager](resource-discovery.md#native-resources-manager).

GTK Admin Users column order, compact rows, read-only flag indicators and relative
password timestamps are frontend presentation. Raw server field names/values
remain in the model and account edit requests; the browser admin editor retains
its existing presentation and the same account fields and permission checks.


Public rig symbols use `rigN`, with N matching the server's media rig index.
Descriptive configuration aliases remain internal identities for settings and
UUID persistence. Object descriptors, inventory/CAT/GPS names, media channel
names and rig rooms use the public symbol, so native/browser controls and linked
station catalogs agree. Explicit numeric configuration aliases retain matching
slots where available; runtime receivers use a free slot without renumbering
existing rigs. Human-readable object names remain descriptive labels.


Native `/exec` runs local shell commands asynchronously using the shared
subprocess helper. `-o` sends literal stdout and `-e` uses the input parser in
the originating server/tab context; stderr stays local. This operating-system
feature has no browser equivalent or new protocol operation. See
[Native client shell commands](client-exec.md).

GTK top/bottom main tabs wrap into multiple rows when space is limited.
Drag tabs to rearrange them across rows; drop on either half of a tab to move
before/after it. The strip reorders the actual notebook pages, so Alt-1 through
Alt-9, Alt-0 (tenth tab), Alt-Left/Right and `/win <number>` follow the visible
order. Displayed shortcut numbers refresh after moving or closing tabs; use
`/win 11` and higher for tabs beyond the first ten. Left/right placement retains
native vertical tabs. Rearrangement lasts for the current client session.
This is GTK-specific presentation; TUI and browser window ordering is unchanged.

TUI Status, Client Log and Host Log each have one shared tab across native
connections. Log/status lines carry source profile prefixes. Ctrl-Tab in any
of these tabs changes the same global selected server used by commands and
shown in their prompts; changing system tabs never changes that selection.
The shared TUI library exposes an optional prompt renderer; rrclient owns
server selection and names. GTK retains its own system-tab presentation and
the browser retains its existing connection presentation.

GTK chat entries retain their original server and room for sends and offline
checks, independently of the selected/reordered notebook page. Common native
input dispatch owns the scoped parser context. Notebook selection refreshes
page-dependent state after GTK updates its current page. Native IRC private
NOTICE/ACTION events retain query classification; GTK query tabs have no
channel roster. This is native frontend/IRC behavior, with no browser change.

Native and browser `/query user [message]` both open the named private tab and
optionally send the remaining text to that user; the native parser splits the
username from the remaining message. GTK F1 opens the shared categorized help
content in a separate window while `/help` retains text output. Privilege
filtering stays in the common renderer. GTK config filtering has a permanent
Filter label and an empty-field hint. Server-associated GTK tabs use a mixed
profile-name hash and a session palette that separates close colors, with
black text on light backgrounds or white text on dark backgrounds. Labels,
keyboard numbers and drag order remain independent of those colors. Shared
system tabs retain the theme; these presentation details are GTK-specific.

PTT admin rows include a persisted user home-server field and server-owned
rig/VFO scope. Both native and browser tables consume those fields; the database
stays per node. GTK column sizing, quota display units/grouping, PTT duration
header and frequency grouping are frontend presentation; raw quota values stay
in seconds/bytes. Config buttons use one GTK row with a blank safety row below.

Server GPS inventory reports configured coordinates as `static`, including
station fallback inherited by rigs. `[site] coordinates` is the station fallback
when `station.gps.position` is empty. Native and browser consumers retain the
same valid/manual GPS flags and manual-mode RMC synthesis; real receiver
no-fix states remain explicit and are not replaced by an invented fix.

## Webcam video

The server owns capture and configured delivery profiles. Native GTK and the
browser share video discovery, subscription and `media.profile` selection;
choices come from advertised `video.<codec>.<fps>` pipeline entries. Matching
viewers share an encoder; other profiles remain independent. Capture and profile
processes run only while needed. Native GTK uses configured fwdsp H.264/H.265/JPEG
decoders and bounded local-image delivery; the browser uses JPEG profiles. TUI
provides media discovery/subscriptions without a graphical video viewer.

## Parser profiling and internal dictionary copies

Native/server builds with `USE_PROFILING` count incoming compact JSON operations,
bytes, rejects and parser time. These process-local diagnostics change no wire
messages and require no browser behavior change. Native object-cache copies
reuse typed `librustyaxe` dictionary cloning; cached values and public behavior
remain the same. See [WebSocket profiling](websocket-profiling.md).

## Source organization

Server relay, event and database domains use separate translation units with
private server headers. Browser chat help, users and commands load after
`webui.chat.js` through the existing ordered asset loader. These are internal
organization changes; commands, protocol messages and client behavior remain
equivalent. See [source organization audit](source-organization.md).

## GTK tab closing and application icons

GTK conversation tabs offer right-click Close and Force close; Force close
requires confirmation with Cancel as the default. Menus retain page identity
across reordering/removal, and the conversation callback resolves that page's
server and room before invoking the existing common `/win close [-force]` path.
Normal online room closing waits for self-PART confirmation; forced closing
forgets local room/media state immediately. Admin, Config, Status, Client log
and Host log pages never opt into these actions. Common close entry points
also reject shared status/log windows in GTK/TUI. The browser's separate room
view already excludes its builtin panels from room removal; this GTK menu adds
no wire messages or browser frontend requirements.

The GTK main window establishes application icon defaults, including its
source-tree file fallback. Ordinary windows and GTK dialogs inherit that same
icon even when they do not register with the window-manager helper.

GTK incoming chat uses an ensure-without-select frontend hook rather than the
explicit join/query selection path. Background room/query messages preserve the
selected page, server and draft; `•` marks unread traffic and bold `!` marks a
case-insensitive nickname mention with nickname boundaries. Viewing the tab
clears both. Self echoes and replay history do not set unread markers. Explicit
`/join`, `/query`, room links and numbered/relative navigation still select tabs.
The browser already creates incoming rooms with `ensure_room(room, false)` and
retains its existing chat highlight behavior; IRC remains native-only.


## Chat presentation and supervision attribution

GTK batches room/HTTP(S) link scanning after formatted text insertion, and opens
HTTP(S) links with the desktop URI handler. The browser already renders HTTP(S)
links. GTK input undo/redo retains formatting tokens (Ctrl-Z, Ctrl-Shift-Z or
Ctrl-Y); this is frontend presentation. GTK and browser append only follow the
chat tail if the reader was at the end. TUI preserves its physical-row offset
while new output arrives. Explicit navigation can still select/scroll a room.

IRC remains native-only. Common roster ordering is channel operators, voiced
users and regular users, alphabetically within each group; operator membership
wins when a user is also voiced. Room nick display uses @/+ and learned IRC
operator status adds ! independently. WHOIS, WHO and user MODE observations
update operator status in the connection context; raw nicknames remain command
identities. Newly created GTK/TUI queries request WHOIS with replies routed into
the query. Existing query selection does not repeat the automatic request.

Supervision's existing random request ID is the grant ticket. Server lifecycle
and associated TX logs retain that ID; PTT records snapshot `supervision_ticket`
and `supervising_elmers` at key-down so later consent expiry/disconnect does not
rewrite history. Existing databases receive nullable columns, leaving historical
rows unattributed. Admin PTT row dictionaries include these fields through the
existing operation. No client permission or wire operation is added.


Native topic notices are rendered by the common handler, including the setter
for changes (including self changes). GTK's topic hook only updates presentation
metadata. IRC 332 displays the initial topic; 333 identifies its original setter
without mistaking the replying server for the author. Query responses identify
the current topic without attributing it to the requester. Browser changes
include the same setter when supplied. IRC QUIT appears in shared rooms only;
native protocol QUIT is directed to its room rather than Status.

GTK and browser timestamps render one font step smaller than message text.
Native timestamp callers use the single separator already supplied by the time
helper. Room sender decoration is contiguous, for example `<@nick>` or
`<+nick>` (and `<!@nick>` for a known IRC oper). Native protocol staff use @ and
noob accounts use +, matching the existing native `/users` convention; browser
room messages mirror that convention. Regular users have no privilege marker.
All TUI prompts use the active conversation's server profile, while shared
system tabs use the globally selected server. Terminal font sizes stay under
terminal control.


RustyRig WHOIS includes `talk.rooms`, a space-separated, case-insensitively
deduplicated union of joined rooms across the account's authenticated sessions.
`librrprotocol/srv.chat.whois.c` reads actual session membership, independently
of optional room/VFO metadata. Native `rrclient/events.c` and browser
`js/webui.chat.js` display a Rooms line, with `(none)` for an empty list; older
servers that omit the field retain their previous output. Connection time,
last-heard time and client identity still describe the first matching session.
IRC WHOIS remains independent and unchanged.


Native media consumers share the `rr_client_media_chan` definition with the
owning cache rather than casting a shorter duplicate record. The previous view
omitted the profiles array before the description/subscription fields, so those
field offsets could report incorrect state. This internal layout correction
changes no wire fields. Common object update subscription status is scoped with
the existing per-server object cache. Explicit native host serial and CAT
attachments retain an originating profile; browser clients have no local
PTY/CAT attachment equivalent. GPS output endpoints retain their existing
active-server behavior.

## Local TX talkback suppression

Native `audio.mute-talkback` defaults to true ("Mute RX stream during TX to
suppress talkback"). The browser exposes the same default in Config and stores
it under the same localStorage key. Both clients suppress playback only for RX
resources on the locally transmitting rig/VFO, including a rig's shared RX
stream. Other users' TX and unrelated RX streams remain audible. Native mixing
checks the originating connection's roster, independent of the selected tab;
decoders keep consuming muted audio rather than saving it for release.

## Account editor persistence and errors

Native GTK and browser Admin editors send only changed editable account fields
in the existing `admin` / `user-edit` JSON payload. The server still accepts
older full-field requests; missing fields are preserved, and read-only identity
and credential metadata cannot be updated. SQLite applies the supplied fields
in one bound UPDATE. Account writes check prepare, bind, step, affected-row and
finalize results; failures include SQLite diagnostics in server logs without
bound credentials. Account removal rolls back on commit failure.

The server replies to editor failures with the existing Admin envelope using
`talk.target=users`, `talk.action=error` and a JSON `message` in `talk.data`.
GTK shows a dialog and persistent table status; the browser shows a dialog.
`changed` is sent only after persistence and active-account reload succeed.
A reload failure explicitly reports that SQLite already saved the account.
Protected-account and elevated-privilege edits still require `owner`.
Fresh SQLite seeds give the bootstrap admin both owner and admin privileges;
existing databases retain their configured grants.

## Account password dialogs and audit timing

GTK and browser account editors expose Set Password (masked new/confirmation
inputs) and Reset PW, reusing `/user pass` and `/user resetpw`. The existing
command grammar accepts 8–128 bytes without whitespace. Temporary-password
notices and password-change results also open requester-local dialogs; GTK
includes the server profile and allows copying the returned credential.
Credentials are not written to the audit record.

Successful account creation, privilege changes, locking/unlocking, removal,
password reset/set and field edits issue `LOG_AUDIT` immediately after database
persistence, before active-state reload. The existing `audit.c` logger callback
persists those events to SQLite even when reload fails. Account editor audit
records name the changed editable fields without their values. Newly generated
credentials are still returned after a successful write if reload fails.

## Source-derived privilege editor

`python3 tools/generate-privileges.py` (or `make privileges`) scans owned C
access-check calls across all build profiles. It generates the native header,
browser catalog and `doc/privileges.generated.json` mapping each privilege to
source locations. `--check` reports stale generated artifacts without writing.
This lexical scan includes literal alternatives and the dynamic `serial.*`
export pattern; it cannot enumerate runtime configuration-specific export names.
It excludes tests, comments, third-party and generated sources. Privileges used
only in old seed/config strings are not invented as active access checks.

Native GTK and browser Users actions and Edit User dialogs provide Edit Privs.
The checkbox dialog is sorted, preselects exact existing grants and includes
custom/unknown existing tokens, preserving them unless explicitly unchecked.
Apply inside Edit User stages the permissions field until Save; the Users tab
sends only a permissions patch. Server validation and owner-only grant rules
remain authoritative. Custom grants can still be entered in the raw account
permissions field. Both frontend catalogs are generated from authoritative C.
