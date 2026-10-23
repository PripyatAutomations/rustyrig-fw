# Original librrprotocol IRC source import

This directory is the historical source snapshot used for the extraction.
**Do not compile it.** The active implementation is the refactored original
code in `librustyirc/`, not an independent replacement parser.

## Ownership and call path

- `irc.c`: original message allocation/tokenization, including full 311 real names.
- `irc.dispatch.c`, `irc.commands.c`, `irc.numerics.c`: original single callback
  registry and wire handlers, now receiving opaque `irc_conn_t` connections.
- `irc.capab.c`: original ISUPPORT, CAP and SASL negotiation.
- `irc.channel.c`, `irc.modes.c`: original channel/user helpers and mode tables.
- `irc.audio.c`, `irc.dcc.c`: original offer codecs; no media or peer sockets.
- `connection.c`: connection-owned receive framing, registration and transport
  callbacks. The application owns the borrowed descriptor and all socket I/O.
- `rrclient/irc.c`, `cli.irc.c`, `irc.compat.c`: RustyRig TCP/TLS and event adapter.
- `rrclient/irc.dcc.c`, `irc.audio.session.c`: CHAT/SEND and AUDIO sessions.

The adapter translates events to librustyaxe's event bus; interested client
components subscribe with `event_on()`. Shared dictionary sends use the existing
`protocol.server.send` event instead of linking IRC into librrprotocol.
`rrserver` does not link librustyirc. USE_RUSTYIRC is generated consistently for
all RustyRig objects because it changes rrconn_t's layout.

## Compatibility and remaining limits

The rrclient adapter retains the original `irc_*` entry points so its GTK/TUI
and command code can migrate separately. These are application interfaces,
not exported librrprotocol APIs. Wire callbacks now receive `irc_conn_t *`.
Historical incomplete features are unchanged: no new IRCv3 tag parser,
reverse DCC, RESUME or SDCC, and channel/UI bookkeeping still consumes the
original event stream. The registry and capability state are single-threaded;
applications must serialize access and must not reset/re-feed a connection inside
an event/send callback. Releasing a connection during a callback defers destruction
until the active library call returns. The library never opens, reads, writes or closes an fd.

## Verification

`make -C librustyirc test` links against librustyaxe only. Integrated IRC,
WHOIS/event, DCC and connection tests live beside rrclient in `rrclient/tests/`.
The root `tests/run-tests.sh` also discovers the librustyirc suite.
