# RustyRig repository guidance for AI coding agents

## Project overview

RustyRig is radio/remote-station software. The main firmware/software
repository contains:

- `fwdsp/` - GStreamer-based audio bridge
- `librrprotocol/` - RustyRig protocol library and wire-level behavior
- `librustyaxe/` - shared C code used by multiple projects
- `rrclient/` - native GTK/TUI client
- `rrserver/` - backend server
- `www/` - web UI integration/submodule

The browser WebUI source itself lives in the separate
`PripyatAutomations/rustyrig-www` repository.

## First rule: understand the architecture before changing behavior

For a client behavior change, determine which of these layers owns the
behavior:

1. `librrprotocol` - protocol/wire semantics
2. `rrserver` - server-side behavior
3. `rrclient` - native client behavior
4. `www` from rustyrig-www project - browser client behavior
5. `librustyaxe` - Shared code used in many of my projects

Do not assume that a convenient implementation location is the
authoritative location. Never try to put UI stuff in the libraries.
You can send an event from the library for the program to consume,
if interested. If the program is interested in an event, it must
use event_on() to listen for it and provide a suitable callback.

## Native C client

`rrclient/` contains multiple UI/front-end implementations:

- common client logic
- GTK UI
- TUI UI
- radio/CAT handling
- connection management
- chat
- VFO/frequency handling

Keep common behavior out of frontend-specific code when possible.

The project uses 3-space indentation for C.

## C / JavaScript parity

The WebUI is a separate implementation of the client. It is not merely a
presentation layer.

When changing client behavior, check both repositories:

- `rustyrig-fw/rrclient/`
- `rustyrig-www/js/`

Observable client behavior should remain equivalent unless a difference
is explicitly documented as frontend-specific.

See `doc/client-parity.md` for the current parity map.

## Important rules

- Check existing project abstractions before adding a new helper.
- Do not casually change protocol messages or their semantics.
- Do not duplicate shared global state between libraries.
- Preserve the non-GTK build.
- Be conscious that some shared code may eventually run on small
  microcontrollers.
- Prefer integer arithmetic when practical; do not introduce floating
  point merely for convenience.
- Use the project's existing time abstractions rather than inventing
  unrelated timestamp/elapsed-time mechanisms.
- Preserve existing error handling and logging conventions.
- Do not remove code just because it appears unused until build profiles
  and alternate clients/frontends have been considered.
- Configuration items MUST be added to defconfig.c in rrclient or rrserver
  to prevent crashes at start without a config.
- Build configuration is in config/${PROFILE}.config.json and PROFILE
  defaults to 'radio'
- mk/json-config.mk maps config settings to make variables as needed

## Before modifying code

For a non-trivial change:

1. Locate the existing implementation.
2. Locate its callers/users.
3. Determine whether the behavior is protocol-defined.
4. Search the other client implementation for corresponding behavior.
5. Check the relevant architecture/parity documentation.
6. Make the smallest change consistent with the existing design.
7. Build/test the affected configurations.

## Synchronization marker

Use `PARITY:` comments when a C and JS implementation must remain
behaviorally synchronized. The comment should name the corresponding
file/function.

Example:

    /* PARITY: rustyrig-www/js/webui.frequency.js */

This marker is intentionally searchable:

    grep -R 'PARITY:' .

## Authoritative vs mirrored code

Authoritative:
- protocol definitions and wire semantics belong in `librrprotocol`
- server behavior belongs in `rrserver` where the server is authoritative

Mirrored:
- client state and user-visible client semantics may exist in both C and JS
- these must be kept behaviorally synchronized

Frontend-specific:
- GTK widgets
- TUI terminal rendering
- DOM manipulation
- browser-only APIs
- CSS
- browser-specific UX

Do not force frontend-specific implementations to look alike. They only
need equivalent behavior where the parity document says they do.
If possible without large changes, we should try to adjust the webui to
match C version as we add new protocol messages or features.
Configuration