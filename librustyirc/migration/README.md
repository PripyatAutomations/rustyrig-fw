# Original librrprotocol IRC source import

These files preserve the original source during the ABI migration. **Do not
compile this directory**: callbacks still use `rrconn_t` and some files call
librrprotocol transport, RTP and RustyRig session APIs.

The actively standalone core is in `../irc.c`, `../connection.c` and their
headers. Its `rustyirc_connection_t` is the `irc_conn_t` opaque type exposed
conditionally through `librustyaxe/struct.h`.

Ownership target:

- librustyirc: wire framing, protocol messages, numerics, CAP/SASL, user
  and channel state, client registration, callback routing.
- rrclient: TCP/TLS, reconnection, UI, credentials policy, DCC AUDIO
  session/media and ordinary DCC file/chat session sockets.
- librrprotocol: no IRC runtime state or IRC message handler definitions.

The import is **not yet a completed move**. The originals are retained in
librrprotocol until all internal signatures and dependencies have been changed
and the parent git submodule references can be advanced coherently. Do not
declare USE_RUSTYIRC for only part of a build: the conditional rrconn_t layout
must be consistent across every object that consumes it.

The media-session implementation was relocated to rrclient and removed from
librrprotocol's latest commit. To test that change, update/pin the
librrprotocol submodule to a commit including that removal, and likewise pin
the updated librustyaxe submodule. Avoid compiling duplicate session symbols
from the old submodule checkout.
