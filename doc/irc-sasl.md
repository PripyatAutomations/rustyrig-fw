# Native IRC SASL

GTK and TUI share IRC SASL negotiation. Configure each IRC server profile:

```ini
[server:libera]
server.url=ircs://irc.libera.chat:6697
server.user=YourNick
server.sasl.mechanism=PLAIN
server.sasl.user=YourAccount
server.sasl.pass=YourPassword
```

SASL credentials are separate from `server.pass`, which still sends IRC PASS.
PLAIN is disabled when `server.sasl.user` is empty. PLAIN sends a Base64-encoded
password; use an `ircs://` endpoint to protect it in transit. EXTERNAL sends an
empty initial response and requires an identity accepted by the server, such
as a certificate configured by the transport; setting the mechanism does not
configure a client certificate.

The client requests SASL during CAP LS, starts AUTHENTICATE after CAP ACK,
and holds CAP END until success, failure or a 30-second exchange timeout.
Responses are split into at most 400 Base64 characters per command; an exact
400-character final chunk is terminated by AUTHENTICATE +. Reconnect resets
the exchange. Authentication status is printed in the server status window.
Unavailable/rejected SASL or failed authentication is reported and registration
continues without a SASL account. Credentials are not included in these notices.

Wire behavior follows the [IRCv3 SASL specification](https://ircv3.net/specs/extensions/sasl-3.1).
