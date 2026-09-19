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
The `pc1T`, `g72T`, `mu1T`, `mu0T`, `opuT` and `oggT` tone IDs are native
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
