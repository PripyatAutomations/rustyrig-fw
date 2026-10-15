# Native IRC DCC chat and files

DCC CHAT and SEND use direct TCP connections, following the
[original DCC protocol description](https://www.irchelp.org/protocol/dccspec.html)
and the [modern DCC reference](https://modern.ircdocs.horse/dcc.html).
These are classic plaintext DCC transports, separate from authenticated RTP
DCC AUDIO. IRC carries the offer; chat messages and file bytes use the peer
socket. Incoming offers never open sockets or create files until accepted.

## Commands

Select the offer's IRC server before accepting or closing a session. `/dcc`
lists pending/active audio, chat and file sessions across all connections. CHAT
and SEND entries have local numeric IDs; commands also accept `#ID`.

```text
/dcc chat NICK
/dcc chat accept ID
/dcc chat msg ID Your message here
/dcc chat close ID

/dcc send NICK /path/to/source file.bin
/dcc get ID
/dcc get ID renamed file.bin
/dcc get ID /path/to/new destination.bin
/dcc send close ID
```

Paths may contain spaces, optionally enclosed in double quotes. `/dcc get ID`
accepts an incoming SEND into `~/Downloads/DCC/`, creating that directory if
missing and using the offered filename's final component. An optional bare
filename overrides the name within that directory; an explicit path selects
another destination. Offered paths cannot escape the download directory, and
empty/dot/control-character filenames are refused. The older
`/dcc send accept ID PATH` form remains supported. Existing files and symlinks
are refused. New files use mode 0600. On failure after acceptance,
partial data remains at the chosen path, with a failure notice. Close cancels
both active sessions and pending offers. Chat lines, including incoming CTCP
ACTION, and transfer status are displayed in the issuing/accepting window.
Server context is preserved while another connection is selected, and drafts
are untouched. `/dcc chat msg` is the explicit way to send direct chat text.

Only matching initiated rig subscriptions may attach DCC AUDIO automatically;
that exception never applies to classic CHAT or SEND offers. Classic DCC has no
peer authentication or payload encryption. The local IRC session provides offer
identity and lifecycle, rather than authentication of the TCP connection itself.

## Client configuration

These keys belong to `[general]`, are registered in `rrclient/defconfig.c`, and
can be changed with `/set` and persisted with `/save`:

```ini
dcc.bind=0.0.0.0
dcc.host=
dcc.port-min=10000
dcc.port-max=10099
```

The issuer picks an unused TCP port from a randomized starting point in the
range. Empty `dcc.host` advertises the local IRC connection's IPv4 address.
Behind NAT, set a reachable public IPv4 address and forward this TCP range.
DCC AUDIO retains its separate `dcc.audio.*` UDP settings. CHAT and SEND work
without GTK, RTP, libSRTP, or GStreamer audio support.

## Protocol and reusable API

`librrprotocol/irc.dcc.h` owns offer parsing, session IDs, sockets, queues, file
descriptor duplication, acknowledgement tracking, timeouts and events. Programs
own path selection, acceptance decisions and UI. Library operations duplicate
caller file descriptors; callers may close their originals after a successful
offer/accept. The receive descriptor must refer to an empty writable regular
file. Initialization registers disconnect listeners; shutdown releases every
session. Polling is bounded and uses nonblocking TCP with the project's
monotonic clock. The library emits JSON `irc.dcc.session` notices and
`irc.dcc.chat` messages through the common event bus.

Offers support decimal or dotted IPv4 addresses, quoted SEND filenames and
optional advertised file sizes. CHAT transmits CRLF-delimited text and also
accepts LF-delimited lines. SEND streams binary blocks up to 4096 bytes and
waits for cumulative four-byte network-order acknowledgements. The sender waits
for the final acknowledgement before reporting completion. Counters are 64-bit
and handle the acknowledgement's 32-bit wrap. Known-size receivers never write
more than the advertised size and fail on early EOF; legacy unknown-size
receivers finish at EOF. Pending/connect attempts and stalled file transfers
expire after five minutes. Active chat ends on close, peer EOF or IRC disconnect.
At most 16 classic sessions exist per client, independently of the audio cap.

Reverse/passive DCC, RESUME, SDCC and IPv6 DCC are not implemented. Unsupported
or malformed DCC offers are consumed without starting a transfer. The WebUI does
not implement IRC or DCC.
