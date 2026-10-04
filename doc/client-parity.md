# RustyRig C / WebUI parity map

The native C client and browser WebUI are separate implementations of
the RustyRig client. They do not need identical source code, but they
must have equivalent observable behavior for concepts marked below.

The WebUI is in the separate repository:
`PripyatAutomations/rustyrig-www`

## Current likely parity areas

| Concept | Native C | WebUI JS | Parity |
|---|---|---|---|
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
Both command completers offer LIST, ATTACH, and DISCONNECT. Native CAT writes
use the same room-scoped protocol APIs as the other native controls. See
[Serial endpoints and rig GPS](serial-interfaces.md).

Server real-port passthrough uses MODEM/`seri` binary frames and native local
PTYs. GPS uses separate read-only MODEM/`nmea` media channels with per-rig
location and station fallback. Native `rig.gps-out` endpoints follow the active
rig and automatically subscribe; browser subscriptions are explicit and emit
`rustyrig:gps-nmea` for integrations. This transport difference is intentional:
the browser cannot create local PTYs. Both implementations validate received
GPS framing/checksums; server coordinates and receiver selection are authoritative.

## Resource discovery

Native GTK/TUI and WebUI share `/rig list`, `/rig subscribe|unsubscribe`
(UUID property updates), and `/gps list|subscribe|unsubscribe <scope>`.
The server supplies the resource tree and permission-filtered serial exports.
`/sercom remote` discovers server exports in both clients; only the native
client can attach local PTYs or serial devices. See [Resource discovery](resource-discovery.md).

## Chat input history

GTK and TUI default to shared command/chat history across windows via
`ui.shared-input-history=true`; false keeps history per window. The browser
also shares input history. GTK Up/Down operates on the originating entry,
restores that entry's unfinished draft after the newest item, and retains up
to 50 entries. GTK and browser suppress consecutive duplicate submissions.
