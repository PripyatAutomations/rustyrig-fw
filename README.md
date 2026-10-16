# RustyRig

**Open-source multiplayer remote radio control and network audio for amateur radio.**

RustyRig connects **operators, radios, and stations** over a network. Control multiple rigs, listen to and transmit audio remotely, share serial and GPS resources with other hams, and link servers across sites — all from a Linux desktop, terminal, or web browser.

Whether you operate a radio from another room, share a station with friends, or connect several remote sites, RustyRig is designed for **collaborative, multi-rig operation** rather than one operator controlling one radio.

## Features

- **Multiple operators, multiple radios:** Share rig/VFO resources in radio rooms, with live operator rosters, text chat, coordinated PTT, permissions, and transmit safeguards.
- **Multi-rig CAT control:** Run independent radios on one server, including hardware supported by [Hamlib](https://hamlib.github.io/) and networked `rigctld` endpoints. Control frequency, mode, bandwidth, and other supported properties.
- **Linked stations:** Connect RustyRig servers across sites to discover remote rigs, join rooms, operate authorized radios, and relay audio and controls over the network.
- **Low-latency network audio:** Independent RX/TX streams, negotiated codecs including Opus, direct RTP where permitted, and WebSocket-based fallback. Mix multiple receive streams with stereo routing and per-stream controls.
- **Three client interfaces:** Native GTK graphical client, text-mode terminal client, and browser-based WebUI.
- **Serial and GPS passthrough:** Access station serial ports and GPS data remotely through the client, alongside radio control and audio.
- **Yaesu-style CAT emulation:** The client can emulate a Yaesu-style CAT interface, allowing compatible ham applications to exchange CAT commands through RustyRig.
- **SDR receivers:** Discover and connect to KiwiSDR receivers for remote **receive-only** listening, with an extensible bridge/driver architecture.
- **Operator and station tools:** IRC integration, station resource discovery, optional recording, server file sharing, account administration, audit and PTT logs, and configurable access controls.

RustyRig is actively developed and currently uses **testing** releases. Features and configuration may change; check the [CHANGELOG](CHANGELOG) for recent work and limitations.

## Screenshots

| GTK client | Terminal client |
| --- | --- |
| ![RustyRig GTK client](doc/screenshot-gtk.png) | ![RustyRig TUI](doc/screenshot-tui.png) |

| GTK configuration | Browser WebUI |
| --- | --- |
| ![GTK configuration](doc/screenshot-gtk-cfgtab.png) | ![RustyRig WebUI](doc/screenshot-webui.png) |

## Getting started

The current installation tooling primarily targets Debian-based Linux systems:

```sh
git clone --recurse-submodules https://github.com/PripyatAutomations/rustyrig-fw.git
cd rustyrig-fw
./install-deps.sh
```

See [QUICKSTART.txt](QUICKSTART.txt) for build, configuration, and startup instructions. Server and client configuration examples are in [`config/`](config/). Consult the in-client `/help` for commands.

## Project components

| Component | Purpose |
| --- | --- |
| `rrserver/` | Multi-user radio, media, and station server |
| `rrclient/` | Native GTK and terminal clients (`-T` for TUI) |
| `www/` | Browser client ([rustyrig-www](https://github.com/PripyatAutomations/rustyrig-www)) |
| `rrbridge/` | External SDR receiver bridge and drivers |
| `fwdsp/`, `libfwdspmgr/` | Audio processing, codecs, and recording |
| `librrprotocol/` | Shared protocol library for building compatible clients |
| `librustyaxe/` | Shared application infrastructure |

## Contributing

Hams, testers, developers, and packagers are welcome. Feedback from real station setups, hardware compatibility testing, bug reports, documentation improvements, and patches are especially valuable.

Browse the [issue tracker](https://github.com/PripyatAutomations/rustyrig-fw/issues) or review the [CHANGELOG](CHANGELOG) to see what is changing.
