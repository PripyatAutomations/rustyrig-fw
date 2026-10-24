# RustyRig

**Share radios, audio, and station resources over a network.**

RustyRig is an open-source, multi-user amateur-radio platform. Operate radios from another computer, listen with friends, or bring multiple stations together. Operators get live radio controls, audio, chat, and access to the resources they are allowed to use.

RustyRig is under active development. The current releases are for testing; features and configuration can change.

## What you can do

- **Operate together.** Join radio rooms, see who's online, chat, and coordinate transmitter access.
- **Control more than one radio.** Work with supported Hamlib radios and networked rig-control backends.
- **Listen and transmit remotely.** Route receive and transmit audio over the network, with codec selection and per-stream controls.
- **Choose your interface.** Use the Linux GTK desktop client, terminal client, or browser interface.
- **Connect stations.** Link servers to access authorized remote rooms and station resources.
- **Explore SDR receivers.** Browse and connect to supported remote receivers through the SDR Directory.

Additional tools include remote serial/GPS access, audio recording, administration, and station resource discovery.

## See it in action

| Desktop client | Terminal client |
| --- | --- |
| ![GTK client](doc/screenshot-gtk.png) | ![Terminal client](doc/screenshot-tui.png) |

| Settings | Browser client |
| --- | --- |
| ![GTK settings](doc/screenshot-gtk-cfgtab.png) | ![Browser client](doc/screenshot-webui.png) |

### Resource Explorer

![Resource Explorer](doc/screenshot-explorer.png)

## Get started

RustyRig currently targets Debian-family Linux distributions for building and packaging.

```sh
git clone --recurse-submodules https://github.com/PripyatAutomations/rustyrig-fw.git
cd rustyrig-fw
./install-deps.sh
```

The dependency helper prints the installation command for your selected build profile. Follow the instructions it produces, then continue with the **[quickstart](QUICKSTART.txt)** to build, configure a station, and connect your first client.

**New to RustyRig?** Start with a single server and one radio—or Hamlib's dummy backend—before enabling additional stations or transmit functions.

## Documentation

| Guide | When to read it |
| --- | --- |
| [Quickstart](QUICKSTART.txt) | First installation, login, and connection |
| [Operator guide](doc/operator-guide.md) | Tuning, audio, rooms, SDRs and troubleshooting |
| [Installation and packaging](doc/INSTALLING) | Package and service installation |
| [Radio rooms](doc/rig-rooms.md) | Multiple rigs, room access, and VFO/audio behavior |
| [Audio codecs](doc/media-codecs.md) | Selecting codecs and setting up audio pipelines |
| [IRC client settings](doc/irc-client.md) | Multiple connections and proxies |
| [Serial devices, server exports and privileges](doc/serial-interfaces.md) | Map physical UART/USB devices, grant per-device access, attach from clients, and understand mesh limitations |
| [Known issues](doc/KNOWN-ISSUES.md) | Open RC problems and requested fixes |
| [Changelog](CHANGELOG) | Recent changes and known transitions |
| [RC release checklist](doc/release-checklist.md) | Build, package, smoke-test and tag a release |
| [Release packaging guide](doc/RELEASING.md) | Debian RC versions, reproducible builds and upgrades |
| [Server maintenance](doc/server-maintenance.md) | Confirmed database, log, recording and identity resets |
| [Remote SDR architecture](doc/remote-sdr-architecture.md) | Design proposal, **not implemented functionality** |

Configuration examples live in [`config/`](config/). Use `/help` in the client for commands.

## NOTES

This is an early version, especially backend.yaesu -- please don't use it
unattended until you test it thoroughly in your setup.

CAT emulator currently has very limited 991a support and you probably should
select ft891 in your program for now if trying to use it.

Yaesu backend has experimental support for ft991(a) support. If it doesnt
work well, let me know and in the meanwhile try hamlib backend.

## For developers

The main pieces are `rrserver` (server), `rrclient` (GTK/TUI), `rrbridge` (SDR bridges), `librrprotocol` (shared protocol), `librustyaxe` (application support), and `fwdsp` / `libfwdspmgr` (audio). The browser UI is developed in [rustyrig-www](https://github.com/PripyatAutomations/rustyrig-www).

Contributions, hardware compatibility reports, documentation improvements, and reproducible bug reports are welcome. See the [issue tracker](https://github.com/PripyatAutomations/rustyrig-fw/issues).
