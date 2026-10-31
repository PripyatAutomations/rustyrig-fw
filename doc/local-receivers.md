# Direct local KiwiSDR in rrclient (initial implementation)

The GTK Receiver Directory now offers **Connect locally** alongside the existing server-backed Connect button. This launches the installed `rrbridge` host with the same `rrbridge-kiwi.so` driver used by rrserver. No RustyRig server, account, room, WebSocket relay, or RTP negotiation is required.

Local PCM travels over a UNIX `SOCK_SEQPACKET` socket, is resampled through fwdsp to the client's 16 kHz RX clock, and enters the existing audio mixer under `local.kiwi.rx`. The stream is visible in RX Routing with L/R/S/M output controls. Successful device availability updates the Receiver Directory's persistent last-connected history.

**GTK controls:** Connect locally / Disconnect local; Tune local (frequency in Hz, USB/LSB/AM/CW); normal RX Routing for audio placement. HTTP and HTTPS directory URLs select the Kiwi bridge's WS or WSS connection respectively. An omitted URL port uses 80 (HTTP) or 443 (HTTPS), not the direct Kiwi receiver default 8073.

**Install:** rrclient needs `rrbridge` on PATH (or `./bin/rrbridge` from a source checkout) and the driver module in `/usr/lib/rustyrig/modules/rrbridge`. The local process uses an ephemeral configuration file; it does not edit the rrserver configuration.

**Current limitations:** The first implementation hosts one local SDR at a time, and does not yet register a full local VFO/resource in Explorer or expose a standalone TUI command. A child process supplies PCM while the GTK event loop polls its bounded IPC sockets; further work should move the PCM drain off the GUI thread. It does not implicitly publish the local receiver into the mesh. The existing server-backed SDR path is unchanged.

**Validation needed:** Full client build; Kiwi direct and proxy endpoint tests; TLS/proxy handshake, switch/local disconnect/restart, RX routing changes, clean process reaping, audio starvation under catalog/UI load.

## Standalone receiver directory and server forwarding policy

The directory can open without any rrserver connection. It immediately displays
the last persisted SQLite catalog and, after the one-hour freshness interval,
refreshes from the configured `receiver-directory.url` HTTPS mirror. If the
mirror fails, the existing cache remains visible. A newly imported server
snapshot also replaces the active catalog and is saved for later offline use.

The **Favorites only** filter defaults off. Favorites are stored by canonical
receiver URL in the local SQLite database, independently of provider catalog
refreshes. The right-click **Mark** menu sets Unreachable, Restricted, Good,
or Clear Mark; Clear Mark deletes the local override.

**Connect** defaults to a local `rrbridge`. The **Proxy rrbridge?** checkbox
defaults off; selecting it uses the rrserver-backed room flow, which can share
one upstream receiver among multiple clients. The server setting
`bridge.proxy-enabled=true` (default) allows this; setting it to `false`
refuses new server-side SDR bridge requests. Local direct connections are
unaffected.
