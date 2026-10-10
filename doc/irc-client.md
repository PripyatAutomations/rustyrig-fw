# IRC and server URLs

Native GTK and TUI use the same server profiles and connection manager.
Each `server.url` must specify its protocol, host and port:

| URL | Transport |
| --- | --- |
| `ws://localhost:8420/ws/` | WebSocket |
| `wss://localhost:4420/ws/` | WebSocket with TLS |
| `irc://chat.example.org:6667` | IRC over TCP |
| `ircs://chat.example.org:6697` | IRC over TLS |

Ports are explicit even for conventional defaults. WebSocket paths and query
strings are preserved. IRC URLs accept an empty path or `/`; put channel
names in `autojoin`. Bracket IPv6 hosts, for example `ircs://[::1]:6697`.
Invalid schemes, missing/invalid ports, embedded credentials, fragments and
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
no nickname. PASS is distinct from IRCv3 SASL authentication; SASL and capability
negotiation are not implemented yet. Autojoin accepts comma/space-separated
channels and `#channel:key` for channels requiring a key.

The client registers immediately after TCP connection or successful TLS
handshake, handles PING/PONG, and displays IRC chat, notices, actions, channel
membership, topics and NAMES rosters through the shared GTK/TUI handlers.
`/join`, `/part`, `/msg`, `/me`, `/notice`, `/topic`, `/list` and `/whois`
use IRC wire commands. Sent messages are displayed locally because capability
negotiation for server echo is not enabled. Reconnect retains joined channel tabs.
TLS uses the same Mongoose backend and CA settings as secure WebSocket.

IRC transport carries conventional chat. RustyRig JSON resource discovery,
CAT/PTT control, binary media and account administration require a WebSocket
connection; JSON-only requests are rejected on IRC connections.

The protocol library emits `irc.message` and command-specific events as JSON
with `msg.cmd`, `msg.prefix`, `msg.argc`, and `msg.arg0` through `msg.arg15`
(`arg0` is the command). Applications consume these with `event_on()`; protocol
handlers contain no UI code. `irc.connected` means IRC registration succeeded,
while `irc.disconnected` marks transport closure. The native connection manager
owns connection state and profile lifetime.

Legacy `[network:NAME]` IRC server lines also require `irc://`/`ircs://` plus
an explicit port and retain that port when saved. The normal connection picker
uses `[server:NAME]` profiles shown above.

The WebUI is outside the IRC scope. It retains its existing WebSocket behavior
and has no IRC parity requirement, including for future work.
