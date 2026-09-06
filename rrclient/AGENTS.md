# `rrclient/` guidance

This directory is the native RustyRig client.

## Important

The browser client is a separate repository:

`PripyatAutomations/rustyrig-www`

Its JavaScript lives under:

`rustyrig-www/js/`

When changing client behavior here, search that repository for the
corresponding behavior before considering the change complete.

## Native frontend structure

GTK and TUI are frontend implementations. Shared client semantics should
not be duplicated between them.

Likewise, do not put protocol/client-state behavior into GTK or TUI code
unless it is genuinely frontend-specific.

## Particularly important areas

- `connman.c` - connection management
- `vfo.c` - VFO/frequency-related client behavior
- `cat.c` and `cat.*.c` - radio/CAT handling
- `cmd*.c` - command parsing and client commands
- `chat*.c` / `m_privmsg.c` - chat behavior
- `events.c` - client event handling
- `gtk.*.c` - GTK-specific presentation
- `ui*.c` - UI abstractions and presentation

When modifying one of the first five categories, check the JS client.
GTK/TUI presentation changes generally do not require JS changes.
We always should add a simple implementation of features when
missing across languages.

Use 3-space indentation in C.

If behavior is deliberately shared with JS, add a searchable `PARITY:`
comment near the implementation.
