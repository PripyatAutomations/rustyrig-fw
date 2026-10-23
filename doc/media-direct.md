# Direct native audio and linked media

Native clients can receive and transmit audio directly to a room's owning server
using authenticated RTP over UDP. The client initiates each attachment and sends
keepalives, including for microphone/TX streams. The owner replies to that learned
address; it does not initiate another connection back to the client.

## Build and configuration

Host RTP support uses libSRTP2 and native POSIX sockets independently of Mongoose.
Install `libsrtp2-dev` before building Debian packages. The build detects it by
`pkg-config`; `WITH_RTP=false` builds without direct-media support. The packet API
is separate from the host socket adapter. ESP32 can retain Mongoose for control
transport and eventually supply an embedded packet-protection/socket backend;
this change does not claim an ESP32 libSRTP build has been validated.

Payload encryption is optional at compile time. The default uses authenticated,
unencrypted SRTP/SRTCP with NULL cipher and HMAC-SHA1-80. Build with
`WITH_RTP_ENCRYPTION=true` to also permit AES-128-CM/HMAC-SHA1-80. Authentication
is required in both modes. The server selects its mode with `media.direct.encrypt`;
a build lacking that mode does not advertise direct media.

Configure these keys in the server's `[general]` section:

```ini
media.direct.enabled=true
media.direct.bind=0.0.0.0
media.direct.port=8422
media.direct.host=radio.example.net
media.direct.encrypt=false
```

`media.direct.host` must be reachable by the client, including through any needed
UDP port forwarding. An empty host leaves direct media disabled. The initial
socket adapter supports IPv4. Restart the server after changing listener settings.
Native clients default to `media.direct.prefer=true`; setting it false selects
WebSocket audio. Browser clients continue using WebSockets.

Use `/media sub -direct <name|uuid|#number>` to require a private RTP referral
from the owning server. The native client then sends authenticated UDP probes to
that endpoint. An unavailable referral or an endpoint that does not answer within
five seconds produces an error; live audio does not silently fall back to
WebSockets in this mode. `/media sub -proxy <channel>` explicitly selects the
WebSocket path. Either option can change an existing subscription, and can appear
before or after the channel name. Omitting the option uses the normal preference
and fallback behavior for a new subscription. Automatic native subscriptions try
owner RTP first, then connected-server RTP for a linked channel, then WebSocket
proxy media. Each silent RTP endpoint has a five-second handshake deadline;
missing or unusable referrals advance immediately. A live endpoint that stops
answering also advances after five seconds. The same order applies to RX and TX.
Explicit `-direct` remains owner-only. Codec setup headers always use the reliable
control path. Browsers support `-proxy` and explain that `-direct`
requires the native client.

## Authorization and lifecycle

Discovery advertises only the endpoint and protection mode. A client asks for
`media.transport=rtp` in its ordinary authenticated channel subscription. The
owner returns the endpoint, a random 30-byte attachment key and SSRC privately in
`media.subscribed`. Session tokens are never sent in UDP packets or public
catalogs. A linked request uses a matching enabled owner-side account and that
account's permissions; the source server cannot import its own privileges.

An authenticated SRTCP receiver report binds the owner's return address. Client
probes run once per second. Only authenticated packets update attachment liveness;
return-address changes require another authenticated report. Microphone packets
must match the subscribed owner channel and pass the existing mute, privilege,
codec, room and PTT checks.

Grants expire after five minutes and native clients renew after four. Renewing an
explicit direct-only TX grant releases an outstanding PTT hold before rotating
its key; automatic linked TX may retain PTT while its proxy path is live. Room
departure, unsubscribe, account invalidation, control-session closure and link loss revoke
attachments. Remote PTT requires a live direct TX attachment or an authorized
proxy-capable TX subscription in the rig's base room. The owner still enforces
account privileges, mute, codec, room, VFO and PTT ownership for every TX frame.
A proxy key-down gets a three-second startup window for microphone audio. After
that, only complete, nonempty TX audio accepted by the owner's normal media path
keeps proxy TX live. Invalid codecs, rejected frames, partial fragments and
repeated key-down/control messages do not extend this window. A three-second
media-liveness watchdog releases transmit even when the control link still exists.
If the backend fails to release a departing virtual session, the server retains
it and retries rather than freeing accounting references while TX remains active.

## Packet profile and fallback

This is a RustyRig RTP payload profile, not standard Opus RTP or RTSP. RTP payload
type 96 carries complete RustyRig binframes using a one-MHz RTP timestamp. The
`RR` extension (profile 0x5252, two words) contains a frame counter, fragment index,
fragment count and total frame length. Datagrams stay within 1,200 bytes; the
maximum assembled frame is 65,535 bytes. Authentication and replay protection
apply to each packet. Reassembly permits fragment reordering, drops incomplete
frames after 100 ms, and discards older completed frames. Large packets and codec
initialization headers use the reliable WebSocket path.

The home server maintains private owner-channel mappings per connected client,
using stream IDs 128–255 for remote channels so local IDs remain distinct.
Owner-side virtual subscriptions keep audio pipelines alive and receive codec
metadata updates. Reliable codec headers travel through private link replies and
then ordinary binary WebSocket frames. Live RX prefers the owner UDP endpoint;
when the attachment is inactive, audio uses the private link relay and the home
WebSocket connection. Link frames are bounded and fragmented, with separate short
reassembly deadlines for realtime data and reliable headers.

The connected server accepts `media.transport=rtp-home` on linked subscriptions,
subscribes to proxy media on the owner's authenticated WebSocket link, and
privately issues a local key/SSRC from its own eligible RTP pool. Client-facing
RTP uses the translated stream ID for both `media.stream` and `media.owner-stream`.
The connected server relays RX and TX over the link; it needs no RTP listener or
UDP reachability on the owner. Its own RTP ACL protects client-facing UDP, while
the rig owner authorizes the relayed account, room, subscription and controls.
Renewals rotate the local grant; PART/unsubscribe/link loss revoke forwarding.
Loss of downstream TX audio releases owner PTT through the same proxy watchdog.

Full WebSocket proxy media supports both RX and TX without RTP on either server.
Native automatic subscriptions retain the preference order above, and explicit
`-proxy` selects this path immediately. Browsers already use WebSockets and can
transmit to linked rigs using their supported codecs. Explicit `-direct` still
requires live owner RTP for PTT and does not permit automatic proxy fallback.

## Verification

`librrprotocol/tests/test_rtp.sh` exercises authenticated/encrypted packet exchange,
maximum-frame fragmentation, reordering, tampering, replay, wrong keys, late
fragments and RTP sequence rollover. `rrserver/tests/test_media_direct_live.sh`
uses three real servers with dedicated TLS, shared TLS and shared plaintext
listeners to test owner-issued private grants, direct RX/TX, fragmented WebSocket
fallback, PTT gating, watchdog release, room departure and tree reconnection.
It also disables the owner's RTP listener to verify full proxy RX/TX and
connected-server RTP over WebSocket links, sustained microphone audio, invalid
codec rejection, proxy startup timeout, permissions, unsubscribe and disconnect.
Fixtures use temporary accounts, databases, configs and ports. They test media
routing and permissions without requiring a physical radio or microphone.
`rrserver/tests/test_media_proxy_live.sh` runs the proxy-only case independently
of SRTP support, including `WITH_RTP=false` builds; the existing component test
runner discovers both wrappers for `make test`.

DSP cleanup must include linked subscribers through the existing media-demand
query. A rig with only remote listeners still needs its encoder after the
periodic subscriber sweep. `fwdsp/tests/test_switching.sh` covers remote-only
demand, departure and warm encoder resumption with real DSP processes.

## RTP port ranges and ACLs

```ini
[rtp]
enabled=true
bind=0.0.0.0
host=radio.example.net
ports=8422-8499
acl=site
encrypt=false
```

`ports` accepts a single UDP port or an inclusive range from 1 through 65535.
At startup the server picks a random starting port, tries the remaining ports
if necessary, and advertises the port it successfully bound. One socket per
pool multiplexes its authenticated attachments; this is not a port per user.
Allow the whole range through the firewall. An exhausted pool is unavailable.

Additional `[rtp:name]` sections have the same keys and must explicitly set
`enabled=true`. Up to 16 pools can listen. Each pool uses its named ACL for both
incoming and outgoing media. The first eligible active pool is advertised.
Disable `[rtp]` when using only named pools. Named pools without an ACL deny.
See [access-control.md](access-control.md) for ordered rules and groups.

For compatibility, empty default-pool fields inherit `media.direct.bind`,
`media.direct.host`, `media.direct.port`, `media.direct.encrypt`, and the
`[listener:media]` ACL. `media.direct.enabled=false` disables every pool.
Changing bind addresses or ranges requires a restart; ACL checks read current
configuration. Hosts may be IPv4 addresses or DNS names.

The origin reports the connected user's numeric IP alongside linked requests.
The owner filters referrals against that address and its own account policy,
so a known denied source is not offered the pool. This address is a discovery
hint from the trusted peer, not proof of UDP source identity: the complete ACL
is checked again against each packet's actual source. If no source is known,
network checks remain provisional until the authenticated UDP probe arrives.
Discovery offers the first eligible active pool; NAT paths where the control
and UDP source addresses differ must both be permitted by the policy.

Each attachment already receives a separate random media key tied to its
verified control session. The control session token is not sent over UDP.
Leaving the room, losing authorization or closing the session revokes the
attachment; keys expire after five minutes. This requires no new client
handshake or protocol fields.

## Native IRC DCC AUDIO

The RustyRig-specific CTCP extension carries a private RTP attachment:

```text
PRIVMSG nick :\x01DCC AUDIO 1 ip port key parent-session channel-uuid ssrc stream owner-stream direction security\x01
```

`\x01` represents the CTCP delimiter byte. `ip` is dotted-decimal IPv4;
`key` is the existing 60-character lowercase media key. Direction is `0` for
RX or `1` for TX, and security is `null-sha1-80` or `aes128cm-sha1-80`.
The two stream IDs retain linked-client versus owner framing. This is an
RTP referral, not conventional DCC TCP audio, and no listening client socket
or inbound NAT mapping is required.

`rrclient/irc.audio.h` provides `irc_dcc_audio_encode`,
`irc_dcc_audio_decode`, and `irc_send_dcc_audio`. Private PRIVMSG and NOTICE
receipts emit `irc.dcc.audio` with normalized `media.subscribed` fields,
`media.parent-session`, and the IRC sender/target metadata. Public channel
messages never emit attachment offers. Unknown versions, malformed addresses,
keys, ranges and extra fields are rejected.

The native client consumes an offer only on its authenticated receiving control
connection, with an exact parent-session match and a channel with a requested subscription
on that connection. Proxy-only channels ignore offers. Normal IRC connections
still need control-session/channel discovery before media can attach; this
extension does not create rig state from unsolicited IRC messages. The server's
existing WebSocket referral path is unchanged.

## Native preference controls

The GTK server editor includes **Prefer direct RTP audio**. It writes that
profile's `media.direct.prefer` override to `~/.config/rrclient.cfg`. The config
editor exposes the global default as a checkbox. Profiles inherit the global
`media.direct.prefer=true` unless overridden.

Use `/media direct on`, `/media direct off`, or `/media direct status` for the
selected server. Changes immediately resubscribe automatic media channels with
the preferred transport. Explicit `-direct` and `-proxy` subscriptions keep their
chosen transport. `/set media.direct.prefer false` changes the global default;
`/save` persists runtime edits to the user config.

## Peer microphone voice over IRC

`/dcc audio NICK [tx|rx]` offers direct client-to-client microphone audio.
Directions refer to the issuing client's microphone/speaker. Omitting the
argument sends an RX offer followed by a TX offer, using independent ports and
fresh keys. It never shares an existing rig grant or imports rig permissions.
Server-to-client v1 referrals remain rig media, authorized by the owning server.
Only a rig referral matching an initiated subscription, its parent control session,
and its channel may attach automatically. Other incoming offers stay pending.
`/dcc` lists pending and active sessions with server, peer, direction and status,
without printing attachment keys.

The recipient sees an offer and runs `/dcc audio accept NICK [tx|rx]`. The recipient's
directions refer to their own microphone/speaker (the opposite of the offered
direction). No microphone packets flow before explicit acceptance and an
authenticated UDP handshake. `/dcc audio close NICK [tx|rx]` ends local attachments.
`/set dcc.audio.mute true` mutes the microphone for all peer calls; `false`
unmutes it. Peer microphone transmission is independent of radio PTT.
Room voice chat and a separate Chat PTT button are future work.

Client settings, registered in `rrclient/defconfig.c`:

```ini
[general]
dcc.audio.bind=0.0.0.0
dcc.audio.host=
dcc.audio.port-min=10000
dcc.audio.port-max=10099
dcc.audio.encrypt=false
dcc.audio.mute=false
```

The issuer chooses unused ports starting at a random position in its configured
range. Empty `dcc.audio.host` advertises the local IRC socket's IPv4 address;
behind NAT, configure a reachable public IPv4 address and forward the range.
The accepting client initiates UDP toward those listeners. Offers expire after
five minutes. Attachments are bound to the local authenticated IRC control
connection and are removed on disconnect. At most 16 pending/active directional
attachments exist per client. Microphone sending stops when peer probes expire.

Voice uses this versioned CTCP payload (v1 remains unchanged):

```
DCC AUDIO 2 ip port key peer-session-id attachment-id ssrc stream owner-stream direction security voice s16l
```

The payload is wrapped in CTCP delimiters and sent privately. Session/attachment
identifiers are fresh peer identifiers, independent of rig control session tokens.
Voice currently carries mono signed 16-bit little-endian PCM at 16 kHz, in
packets of at most 20 ms, inside the existing RustyRig RTP framing. The `s16l`
label distinguishes this PCM from rig codecs. It is a RustyRig extension, not
interoperable with generic DCC chat or standard RTP players. Optional encryption
requires `WITH_RTP_ENCRYPTION=true`; authenticated packet protection remains in
unencrypted builds. IRC carries the private attachment keys, so use TLS IRC
when key confidentiality is required. Browser clients retain WebSocket audio.

Peer attachment allocation, RTP transport, expiry, and enumeration live in
`rrclient/irc.audio.session.h`. Applications register event handlers for
incoming offer notices, PCM frames, and per-attachment closure, and choose when
to call `irc_audio_accept()`. The native client owns microphone/speaker devices
and its configuration. Classic DCC file transfers and chat are documented in [irc-dcc.md](irc-dcc.md).
The IRC implementation remains in librrprotocol.
