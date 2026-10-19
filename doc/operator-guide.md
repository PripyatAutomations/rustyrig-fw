# RustyRig operator guide

RustyRig lets several people use radio and station resources over a network. You can use the GTK desktop interface, terminal client (`rrclient -T`) or WebUI. What you can control depends on the connected server, radio hardware, and your account permissions.

For installation and your first login, start with the [quickstart](../QUICKSTART.txt).

## Connect to a station

Open the client and choose an enabled server profile. Once connected, the station lobby is a place for chat and finding available resources; joining a lobby does not key a transmitter.

Use the room list to find available radio rooms or type `/join #station-rig0` (substituting your station's name). `/part` leaves the current room. Use `/help` at any time for available commands.

If the station has more than one rig, choose the room belonging to the radio you want. Your controls and default audio follow the selected radio context. Some rooms are intentionally receive-only.

## Tune and listen

1. Join a room with a radio or SDR VFO and select the VFO you want.
2. Check the displayed frequency and mode against the radio before changing them.
3. Choose a receive codec supported by the server and client (Opus is a good initial choice).
4. Turn up the receive audio and check the per-stream mixer or routing settings if you hear nothing.

Supported SDR receivers appear in **SDR Directory**. You can connect to a receiver and listen in an SDR room. The receiver's available tuning controls depend on its backend and your permission to operate it.

An RX-only VFO has no PTT or transmit-only controls. Some receive rooms deliberately do not allow independent retuning, because one RF frontend may be shared among listeners.

## Transmit

Transmit only when the station and your account permit it. Confirm the selected rig, VFO, frequency, audio device, and PTT behavior first; start with receive tests. RustyRig coordinates transmit access, but it is not a substitute for the operator's regulatory obligations or hardware interlocks.

If PTT is disabled or unavailable, check that you are in the radio's controlling room and that the backend supports and authorizes TX. A receive-only room cannot gain TX access just by changing the client interface.

## Work with rooms and audio

Rooms organize chat, VFO access, and media subscriptions. They do not necessarily correspond one-to-one with physical radios. Multiple people may listen to the same stream; advanced setups can also mix several receive streams with separate volume and stereo placement.

In the GTK client, VFO controls may dock at the top or bottom (configured by `ui.gtk.vfo-on-top`) or float in a desktop window. Use the dock handle to collapse or expand the controls; the floating window has a **Dock VFO in room** action. Behavior may evolve as the interface is refined.

For multi-rig configuration and detailed room permissions, see [radio rooms](rig-rooms.md). For codecs and audio pipelines, see [audio codecs](media-codecs.md).

## Common problems

| Symptom | Where to look |
| --- | --- |
| Cannot connect | Confirm profile URL, server status, account and TLS settings |
| Radio controls missing | Join an authorized radio/SDR room and select the correct VFO |
| No receive audio | Check room subscription, codec, local output, stream volume and server audio pipeline |
| Cannot tune | Confirm VFO permissions, receiver coverage and whether another operator holds control |
| Cannot transmit | Check TX-capable hardware, controlling room, account permission and active PTT interlocks |
| Browser sign-in fails remotely | Use HTTPS with a valid secure browser context |

For help with a bug, capture the release/build version, operating system, GTK/TUI/WebUI frontend, relevant log messages, what you expected and what happened. Remove passwords, tokens and private account information before sharing logs.

## Next steps

- [Quickstart](../QUICKSTART.txt): installation and first station
- [Installation](INSTALLING): source builds and Debian packages
- [Radio rooms](rig-rooms.md): VFO mappings, permissions and multi-rig audio
- [Media codecs](media-codecs.md): codec and pipeline setup
- [IRC / network profiles](irc-client.md): connection URLs and proxies

Some experimental features are under active development. See the [changelog](../CHANGELOG) for changes to your build.
