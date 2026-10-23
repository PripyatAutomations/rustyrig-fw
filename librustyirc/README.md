# librustyirc (extraction in progress)

This directory is the destination for the reusable IRC client library. The intended
public dependency is **librustyaxe only** (logging, event dispatch and utilities);
there must be no dependency on librrprotocol, rrserver or rrclient.

## Migration boundary

- Core: IRCv3 line framing, prefix/tags/parameters, message parsing/encoding,
  numerics, CAP negotiation, channel and user modes, and callbacks.
- Adapter: socket connection policy, reconnects, TLS, application identity,
  interface display and commands stay with the embedding application.
- RustyRig-specific IRC audio, DCC media sessions and rrprotocol transport
  code remain adapters until they can depend only on the new public API.
- No public callbacks should require rrconn_t; use a library-owned opaque
  connection handle and an application context pointer.

Migrate source and headers from the librrprotocol Git submodule in coordinated
steps, with temporary forwarding headers if necessary. Do not compile the same
IRC implementation into both shared libraries: that would introduce duplicate
global callback registries and runtime state.

The 311 RPL_WHOISUSER format is:
`:server 311 <recipient> <target> <username> <hostname> * :<realname>`
The literal `*` is a placeholder, and the final trailing parameter must
retain embedded spaces.

Reference: https://dd.ircdocs.horse/refs/numerics/311

## Current implementation (migration checkpoint)

The standalone `irc.c`/`connection.c` library builds independently of
librrprotocol and exposes an application-provided transport send callback,
incoming byte framing, nick/registration state, parsed messages, and a
separate DCC AUDIO offer callback. Run `make -C librustyirc test`.

The **old** `librrprotocol` IRC transport, CAP, numeric and channel code
still needs conversion to this connection API before its files can be
deleted. Avoid binding its `rrconn_t` into librustyirc. A parallel
legacy IRC implementation remains active in RustyRig during migration.

DCC AUDIO media sessions were moved from
`librrprotocol/irc.audio.session.c` into `rrclient/irc.audio.session.c`;
the protocol side's offer decoder still emits `irc.dcc.audio`, and
the client remains responsible for accepting offers and media I/O.

The parent repository's librrprotocol Git submodule must be advanced to
the matching librrprotocol commit before relying on the split build.
