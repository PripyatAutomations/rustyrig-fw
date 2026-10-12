# v1.0 readiness

The release candidate keeps the existing WebSocket control protocol, adds tree
server links, owner-authorized remote rig control and native direct RTP audio,
and consolidates the shared subprocess API before external development starts.
WebTransport, mesh routing, home-server UDP relays and browser remote TX remain
future work. See server-linking.md, media-direct.md and subprocess-api.md.

## Automated checks

Build the configured applications and run `make test`. Its default suites cover
shared helpers, Debian conffiles/permissions, librustyaxe, librrprotocol, rrserver,
rrclient, fwdsp, libfwdspmgr and the browser client. Fixtures use local libraries
and temporary configs/databases; they must not provision installed services.

For native RTP, install the libsrtp2 development dependency and build normally.
Also check `WITH_RTP=false USE_GTK=false` and the optional
`WITH_RTP=true WITH_RTP_ENCRYPTION=true` build. Restore the desired release build
after changing these options. RTP encryption is considered only inside an RTP
build. The shipped server config requires a reachable media.direct.host before
advertising a direct endpoint.

The four-server media fixture verifies dedicated TLS, shared TLS and shared
plaintext links, private RTP referrals, native RX/TX, WebSocket fallback, owner
permissions, UDP-loss PTT release, room departure and link reconnection. Codec
and media tests complement those routing fixtures. A passing automated fixture
is not a physical-radio measurement.

## Two-machine acceptance

Before tagging v1.0, test the built Debian packages on the two stations:

1. Install/upgrade with an edited global config; verify dpkg offers to preserve
   it. Confirm the client editor writes only the user's configuration.
2. Enable matching peers and confirm concurrent autoconnect produces one link.
   Join a remote rig room and check LIST, roster, chat and owner VFO state.
3. Test actual receive sound with `/media sub -direct <channel>` and
   `/media sub -proxy <channel>`, including codec changes. Confirm the direct
   endpoint uses the owning server's reachable UDP address.
4. Test microphone audio and PTT into a safe load. Check owner permissions,
   key-up, mute, room departure, client closure, UDP loss and link loss. Observe
   the physical radio's release as well as the displayed state.
5. Restart a peer. Confirm fresh state and subscriptions return and no PTT or
   control command is replayed. Repeat with the native GTK and TUI frontends;
   check browser remote receive audio over WebSockets.

Record machine/package versions, codecs, connection topology and results before
choosing the release version and publishing artifacts. Snapshot configured
state/database data through the normal operator backup procedure before upgrades.
