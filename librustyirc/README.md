# librustyirc

Reusable IRC protocol code extracted from the original librrprotocol sources.
Its only project dependency is librustyaxe. See [the migration ownership map](migration/README.md).

The public API is in `irc.h`, `connection.h`, `protocol.h`, `irc.channel.h`,
`irc.modes.h` and `irc.dcc.h`. `internal.h` is private. The connection is opaque;
applications pass an already-open fd to `rustyirc_connection_new(fd, &callbacks)`,
supply a send callback, and consume protocol events. The send
callback must accept the whole CRLF-terminated line into an application-owned
queue or return false. Feed received bytes after TCP/TLS processing. The library
never closes its borrowed fd, including on reset or destruction. Credentials
are copied into connection-owned storage; callers retain their input strings.

Registration retains the original PASS, CAP LS 302, NICK and USER sequence.
Numeric 311 retains its literal `*` and complete trailing real name. DCC AUDIO
is validated before emitting `irc.dcc.audio`; malformed/public offers are
consumed without being exposed through ordinary chat/message events.

Build with `make -C librustyirc`; run `make -C librustyirc test`. For an installed
librustyaxe SDK, override `AXE_CPPFLAGS` and `AXE_LDFLAGS`. In this workspace they
point at the configured librustyaxe checkout. The parent build produces
`librustyirc.so.0` and links it only to rrclient. Socket/TLS/reconnect policy and
all DCC media/file/chat sessions remain in rrclient. Existing MIT/GPL source
license notices are preserved.
