# RustyRig WebUI guidance

This repository is the browser client for RustyRig.

The native/reference client lives in the separate repository:

`PripyatAutomations/rustyrig-fw`

with native client code under:

`rrclient/`

## Important

The WebUI is a parallel client implementation. Do not treat it as merely
a visual frontend.

When changing client behavior, search the C client for corresponding
behavior.

See `doc/client-parity.md` for the parity map.

## JavaScript layout

The application code is primarily under `js/`.

Important application files include:

- `webui.js` - central WebUI behavior
- `webui.frequency.js` - frequency-related behavior
- `webui.rigctl.js` - rig/control behavior
- `webui.chat.js` - chat behavior
- `webui.chat.completion.js` - chat completion behavior
- `webui.auth.js` - authentication
- `webui.input.js` - input/command handling
- `webui.audio.js` / `webui.audio.framing.js` - audio behavior
- `webui.filexfer.js` - file transfer
- `webui.notifications.js` - notifications
- `webui.syslog.js` - system log
- `webui.winman.js` - window/UI management

Third-party libraries such as jQuery and minified dependencies are not
application parity targets and normally should not be modified.

## Parity

Shared behavior must remain compatible with:

`rustyrig-fw/rrclient/`

Observable behavior includes:

- protocol messages
- command semantics
- state transitions
- validation
- frequency behavior
- connection behavior
- user-visible errors and events

Frontend-specific DOM/CSS/browser behavior does not require source parity.

Use `PARITY:` comments for important coupled implementations.
