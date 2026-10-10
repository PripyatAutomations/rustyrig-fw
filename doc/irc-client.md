# IRC and server URLs

Native GTK and TUI use the same server profiles and connection manager.
Each `server.url` must specify its protocol and host. Ports are optional:

| URL | Transport |
| --- | --- |
| `ws://localhost:8420/ws/` | WebSocket |
| `wss://localhost:4420/ws/` | WebSocket with TLS |
| `irc://chat.example.org:6667` | IRC over TCP |
| `ircs://chat.example.org:6697` | IRC over TLS |

Omitted ports default to 8420 (WS), 4420 (WSS), 6667 (IRC), and 6697 (IRCS).
Explicit ports override these defaults. WebSocket paths and query
strings are preserved. IRC URLs accept an empty path or `/`; put channel
names in `autojoin`. Bracket IPv6 hosts, for example `ircs://[::1]:6697`.
Invalid schemes, invalid ports, embedded credentials, fragments and
whitespace are rejected before connecting. Credentials belong in profile keys.

```ini
[server:chat]
server.enabled=true
server.url=ircs://chat.example.org:6697
server.user=MYCALL
server.pass=
autojoin=#rustyrig,#radio
```

For IRC, `server.user` is the nickname and `server.pass` is the optional server
PASS password. `irc.nick=nonick` is the built-in fallback when the profile has
no nickname. PASS is distinct from IRCv3 SASL authentication; SASL is not
implemented yet. Autojoin accepts comma/space-separated
channels and `#channel:key` for channels requiring a key.

The client starts capability negotiation and registration after TCP connection
or successful TLS handshake, handles PING/PONG, and displays IRC chat, notices, actions, channel
membership, topics and NAMES rosters through the shared GTK/TUI handlers.
`/join`, `/part`, `/msg`, `/me`, `/notice`, `/topic`, `/list` and `/whois`
use IRC wire commands. Sent messages are displayed locally because capability
negotiation for server echo is not enabled. Reconnect retains joined channel tabs.
TLS uses the same Mongoose backend and CA settings as secure WebSocket.

The roster shows IRC privilege symbols from NAMES and subsequent MODE changes,
using the server's PREFIX mapping (normally `@` for operators and `+` for voice).
The TX, Mute and Role columns are hidden unless the connection acknowledges the
RUSTYRIG capability or advertises the RUSTYRIG ISUPPORT token. Capability removal
hides them again; WebSocket rosters retain their RustyRig columns. The client
requests `multi-prefix` when offered so combined op/voice modes remain available.
Other advertised capabilities, including SASL, are not requested.

In a room, plain text followed by Tab cycles through matching room members.
For example, `Ro` completes to `Rob: ` at the start of a message. Subsequent
Tabs cycle through other matching nicknames. Typing or moving the cursor starts
a new completion; command completion requires a leading `/`.

`/quit` sends `QUIT :reason` to all registered IRC connections, including
background servers, then drains their output before closing sockets. Shutdown
waits at most one second for output to drain, so an unresponsive connection
cannot prevent the client from exiting.

## SSH SOCKS proxy

Native GTK and TUI can send any of the four transports through an existing
`ssh -D` SOCKS5 proxy, without an additional library:

```ini
[server:chat]
server.url=ircs://chat.example.org
server.proxy=socks5h://localhost:1080
server.user=MYCALL
```

`server.proxy` accepts `socks5h://host[:port]` or `socks5://host[:port]`;
both send destination hostnames to the proxy for remote resolution. The
proxy port defaults to 1080. IPv6 proxy addresses use brackets. SSH's
dynamic forward requires no proxy credentials. Other SOCKS5 proxies can
use separate `server.proxy.user` and `server.proxy.pass` keys; both must be
nonempty and at most 255 bytes when authentication is enabled.

Set these keys in a `[server:name]` profile for that connection, or in a
`[general]` section as defaults for all connections, including ad hoc `/server`
URLs. A nonempty server-section setting overrides the global setting; an
empty section setting inherits it. For a standalone connection, use:

```
/server -proxy socks5h://localhost:1080 irc://irc.example.net
```

The `-proxy` option can appear before or after the server URL/name and
has highest priority. Without a command option, section setting or global
setting, the connection is direct. The selected status server does not
supply proxy settings for a new standalone connection.
A connection retains its required proxy across retries and reconnects,
even if its configuration keys are subsequently cleared. Restart the client
to deliberately remove that retained proxy setting.

Invalid settings, unavailable proxies, authentication failures, rejected
CONNECT requests and handshake timeouts fail closed: no direct destination
connection is attempted, and registration/TLS/WebSocket traffic waits for
successful proxy negotiation.
The server URL, default destination ports and WebSocket path remain unchanged.
TLS starts after the tunnel opens and retains the destination hostname and
existing CA settings. Proxy negotiation has a 30-second timeout and uses the
normal per-server reconnect handling.

The reusable implementation lives in `librustyaxe/socks.h` and `socks.c`,
without IRC or client dependencies. Other Mongoose consumers, including
rrserver, can initialize `rr_socks_t` with a proxy URL, optional credentials,
and destination host/port, then connect only to its `url`. Feed transport
events to `rr_socks_event()`; `1` opens the tunnel, `0` means negotiation is
pending, and `-1` means failure with the socket marked for closing. Only
start application I/O after `1`; failed initialization must abort connection.
No rrserver proxy configuration is introduced by this move.

SOCKS4, UDP relay and GSSAPI are not
implemented. Browser proxy configuration belongs to the browser/OS;
the WebUI does not consume these native settings.

IRC transport carries conventional chat. RustyRig JSON resource discovery,
CAT/PTT control, binary media and account administration require a WebSocket
connection; JSON-only requests are rejected on IRC connections.

The protocol library emits `irc.message` and command-specific events as JSON
with `msg.cmd`, `msg.prefix`, `msg.argc`, and `msg.arg0` through `msg.arg15`
(`arg0` is the command). Applications consume these with `event_on()`; protocol
handlers contain no UI code. `irc.connected` means IRC registration succeeded,
while `irc.disconnected` marks transport closure. The native connection manager
owns connection state and profile lifetime.

Legacy `[network:NAME]` IRC server lines also require `irc://`/`ircs://`, accept
optional ports and save their resolved port explicitly. The normal connection picker
uses `[server:NAME]` profiles shown above.

The WebUI is outside the IRC scope. It retains its existing WebSocket behavior
and has no IRC parity requirement, including for future work.

## Multiple native connections

`/server localhost` connects the `[server:localhost]` profile without closing
other sessions. `/server irc://localhost` opens an ad hoc connection using the
hostname as its name. A URL matching a configured profile uses that section's
name instead. Distinct endpoints sharing a hostname need distinct configuration
section names. `server.auto-connect` can list multiple profile names.

Status output shares one tab and starts with `|servername|`. On the status tab,
Ctrl-Tab cycles through live/connecting servers to select where commands such
as `/join #whatever` go. Its Send button reads `Send|servername|`; room/query
buttons read `Send`. Commands in room/query tabs always use that tab's server.
The server chooser also adds connections; `/disconnect` closes the selected
server without affecting others. Ctrl-Tab does not change the command server
outside the status tab. Terminals must encode Ctrl-Tab distinctly for the TUI
shortcut to work.

Each server retains its own login/token, reconnect schedule, rooms, roster,
VFO observations, object inventory and media metadata. Shared audio/serial
outputs follow the selected server; background text messages continue to update
all connections. Frontends can use `rrclient_connection_iter/find/select/name`
and the `client.server.selected` event to present connection selection. The
native connection manager owns connection lifetime; UI tabs store stable
profile names rather than socket pointers.


`/server` opens the native server picker (or lists profiles in the TUI).
Profiles are discovered from `[server:name]` sections containing `server.url`;
`server.user` is optional for IRC. `/server name` uses the configuration loaded
by this process. To select a specific configuration file, launch with
`rrclient -f /etc/rustyrig/rrclient.cfg`; `/reload` reloads that same file.
The client reports the loaded file when a named profile has no `server.url`.

Before the server welcome, NOTICE and PRIVMSG output goes to shared `status`,
with the configured server-name prefix, without opening query tabs. After
registration, normal chat routing resumes. `/disconnect` cancels pending
retries and closes that connection's server notice tabs. The default shared
`status` tab remains open; unexpected drops retain tabs for reconnects.

`/server` lists configured profiles and connected/connecting servers in shared
status, marking the selected connection, and opens the GTK picker. An
unnamed `/disconnect` is allowed only from shared `status` and disconnects
the selected server. `/disconnect name` works from any conversation without
changing the selected connection.

`/win close` protects shared `status`. It closes query tabs locally; joined
room tabs send PART to their owning server and close on self-PART confirmation.
A failed PART keeps the room open. `/win close -force` attempts PART and closes
immediately, even if sending fails or no response arrives; it still protects
`status`. Closing an offline room also removes its
pending rejoin request. `/win number` retains window switching.
