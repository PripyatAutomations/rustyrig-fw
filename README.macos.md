# Experimental macOS / Darwin build (client + server)

Status: initial source/build portability changes only. **Not compiled or run on
macOS yet.** A tester should expect compiler and linker failures and report
them verbatim. No macOS server or client release is being claimed.

The goal is native **arm64 or x86_64** builds using Apple's Command Line Tools,
Homebrew, GNU Make and the existing RustyRig source tree. Linux remains the
primary tested platform. No Windows tools or Wine are required.

## Setup

1. Install Apple's Xcode Command Line Tools:

       xcode-select --install

2. Install [Homebrew](https://brew.sh/), then in a Terminal:

       brew install make pkgconf jq glib ncurses openssl gtk+3 gstreamer \
           sqlite curl wslay libusb

   Optional later: `brew install hamlib srtp`. The first Darwin profile
   disables Hamlib modules and Linux-specific hardware bring-up.

3. Clone with submodules:

       git clone https://github.com/PripyatAutomations/rustyrig-fw.git
       cd rustyrig-fw
       # Only fetch a third-party source when enabling its optional feature.
       # ./tools/update-optional-sources.sh

4. Set up the Homebrew toolchain in the **same shell**:

       export PATH="$(brew --prefix)/opt/make/libexec/gnubin:$(brew --prefix)/bin:$PATH"
       export PKG_CONFIG_PATH="$(brew --prefix)/opt/openssl/lib/pkgconfig:$(brew --prefix)/opt/ncurses/lib/pkgconfig:$(brew --prefix)/lib/pkgconfig"
       make --version
       cc --version
       pkg-config --modversion glib-2.0 gtk+-3.0 openssl

5. Try an initial client, server and DSP build:

       make PROFILE=darwin -j2 world

   Source libraries are still named `.so` for compatibility with the current
   module configuration, but on Darwin the shared objects are linked as Mach-O
   images with install names and native `@rpath`. Do not rename these by hand.

   The test profile intentionally disables extra callsign tooling, GPIO,
   Hamlib, Linux I²C drivers and the auxiliary Kiwi bridge/standalone pipeline
   editor. The main client, main server, GTK module and fwdsp are still targets.
   Native TLS and networking remain enabled for subsequent testing.

## First test sequence (once build succeeds)

       ./bin/rrclient -h
       ./bin/rrserver --help

   Verify library and frontend loading before attempting to connect:

       otool -L bin/rrclient
       otool -L bin/rrserver
       otool -L bin/rrclient-gtk.so

   The default Linux paths in `config/rrclient.cfg` and server configuration
   are **not** suitable for macOS. Supply a temporary, explicit configuration
   whose `path.modules` points to an existing module directory and whose
   database and help paths are user-writable. Use the project's supported
   config-file option to select it. Do not create a real transmitter backend
   or key an attached rig in this early smoke test.

## Known porting work still needed

- Verify `librustyaxe` / `librrprotocol` C portability on the tester's
  macOS SDK; library rule changes alone do not prove they compile.
- Fix any remaining non-Apple linker switches from optional objects/tools.
- Resolve GTK frontend dynamic symbols, module discovery, and macOS
  `@rpath` behavior on a real machine.
- Audit `fwdsp` process supervision and GStreamer CoreAudio pipelines.
- Review Darwin PTYs/serial port naming and permissions (`/dev/cu.*`).
- Replace Linux-only webcam / hardware feature assumptions before enabling
  those features on macOS.
- Provide a macOS runtime configuration, repeatable test harness, and
  application packaging after native startup works.

When a test fails, please send the **first actual error**, full compile or
link command, `uname -a`, `brew --prefix`, `cc --version`, the command you
ran, and whether the machine is Apple Silicon or Intel. Avoid `make -i`:
ignored compiler failures can leave corrupted/stale build artifacts.
