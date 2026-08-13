# Copilot Instructions for rustyrig-fw

## Project Overview

RustyRig is a remote ham radio station control system. It consists of multiple interconnected components:

- **librustyaxe**: Core utility library providing config system, logging, TUI framework, JSON handling, event bus, and module loading
- **librrprotocol**: Protocol layer implementing HTTP/WebSocket API, IRC protocol for site linking, authentication, and codec negotiation
- **rrserver**: Backend daemon that manages radio hardware control via Hamlib, audio processing, MQTT, GPIO, thermal management, and user sessions - it is the radio firmware
- **rrclient**: GTK/TUI client for remote station control (use `-T` flag to force TUI mode)
- **fwdsp**: GStreamer-based audio bridge for codec handling and audio pipelines for rrserver and rrclient, providing stability even if gstreamer crashes
- **www**: Web UI (separate repo, served by rrserver on ports 8420/4420)

## Build and Test Commands

### Build

```bash
# First-time setup: install dependencies and submodules
sudo ./install-deps.sh
git submodule init
git submodule update --depth=1

# Full build (from root)
./build.sh

# Or directly with make
make -j$(nproc) world
```

### Run/Test

```bash
# Start server (handles audio, GPIO, radio control)
./test-server.sh

# Run client (separate terminal)
./test-client.sh -T         # Force TUI mode
./test-client.sh            # GTK mode (if available)

# Launch both in tmux/screen windows
./test-all.sh

# With debugging
./test-server.sh gdb        # Run server under gdb
./test-server.sh valgrind   # Run server under valgrind
./test-client.sh gdb        # Run client under gdb
```

### Code Quality

- Uses **uncrustify** for code formatting (config in `.uncrustify.cfg`)
- No automated linting in build pipeline; style is maintained manually
- Compilation flags include extensive error checking and debug symbols by default

## Architecture Patterns

### Configuration System

- **Primary source**: `config/*.config.json` files (e.g., `config/radio.config.json`)
- Uses JQ for JSON manipulation during build (`mk/json-config.mk`)
- Generated header: `build/$PROFILE/build_config.h` (created from config at build time)
- Runtime config files in `~/.config/` override system defaults
- Use `cfg_get_*()` from librustyaxe to access config values

### Build Configuration

- **Profile-based**: Default profile is "radio" (set via `$PROFILE` environment variable)
- **Platform detection**: Automatically detects POSIX vs MSYS2/Windows
- Platform-specific code uses:
  - `ifeq (${PLATFORM},posix)` for Linux-specific features (GPIO via libgpiod)
  - `OS := MINGW64` or `OS := MSYS` for Windows builds
  - `-DMG_ARCH=MG_ARCH_WIN32` or `MG_ARCH_UNIX` compile flags

### Event System

- Core event bus in librustyaxe (`event-bus.c/h`)
- Most subsystems emit JSON events rather than direct callbacks
- Event handlers register via module loading system
- Key todo: Complete JSON event wrapping and document all message types

### Library Dependencies (Submodules)

- **ext/libmongoose**: HTTP server framework (embedded in librrprotocol)
- **ext/mbedtls**: TLS/crypto for HTTPS and WebSocket security (nyi)
- **ext/wslay**: WebSocket protocol layer (nyi)
- **ext/sqlite**: Database (optional, compile-time flag `USE_SQLITE`)
- **librustyaxe** & **librrprotocol**: Both are submodules; pull before building

### Protocol Layers

- **HTTP**: Handled by mongoose (embedded in librustyaxe), serves WebUI
- **WebSocket**: Used by clients for real-time updates
- **IRC**: Eventually will be used to link multiple rigs together
- **CAT (Computer Aided Transceiver)**: Via Hamlib for radio hardware control

## Key File Organization

```
mk/                    # Build system (modular makefiles)
  compile.mk          # Compiler flags, platform detection
  database.mk         # Database/EEPROM handling
  json-config.mk      # JSON config processing
  install.mk          # Install rules
  win64.mk            # Windows packaging
  audit.mk            # Audit/valgrind targets
  
librustyaxe/          # Core utility library (submodule)
  dict.c/h            # Key-value store
  config.c/h          # Configuration interface
  event-bus.c/h       # Event system
  logger.c/h          # Logging
  tui.c/h             # Terminal UI framework
  module.c/h          # Module loader
  mongoose.c          # HTTP server
  json.c/h            # JSON handling
  
librrprotocol/        # Protocol layer (submodule)
  http.c/h            # HTTP API endpoints
  irc.c/h             # IRC protocol
  auth.c/h            # Authentication/authorization
  codecneg.c/h        # Codec negotiation

modsrc/mod.backend.hamlib/
  backend.hamlib.c    # Hamlib integration

modsrc/mod.ui.gtk/
  gtk.*.c             # GTK UI components for rrclient
  
rrserver/             # Backend daemon
  main.c              # Main event loop
  backend.c/h         # Radio hardware abstraction
  channels.c          # Channel memory
  database.c          # SQLite operations
  gpio.c              # GPIO control
  mqtt.c              # MQTT support
  
rrclient/             # Client application
  main.c              # Main loop
  tui.input.c         # TUI input handling
  ui.c                # UI abstraction layer
  
fwdsp/                # Audio processor
  fwdsp.c             # GStreamer pipeline management
  
config/               # Configuration templates
  radio.config.json   # Main radio config
  *.cfg               # Server/client configs
  http.users          # HTTP auth file
```

## Important Conventions

1. **PROFILE environment variable**: Controls which config file is used
   - Affects build output location: `build/$PROFILE/`
   - Affects runtime config location
   - Default: "radio"

2. **Naming**: 
   - `rr*` prefix = RustyRig specific (rrserver, rrclient)
   - `fw*` prefix = Firmware/embedded (fwdsp, but running as userland daemon)
   - Files often include subsystem in name: `gtk.*.c`, `backend.*.c`, `irc.*.c`

3. **Object files**: Organized by subsystem in build directory
   - `build/radio/rrserver/au.o`, `build/radio/rrclient/gtk.core.o`, etc.

4. **Compilation**: Uses `.c` source with inline headers (no separate compilation units per header)
   - Headers in `inc/librustyaxe/`, `inc/librrprotocol/`
   - Source in respective directories

5. **Debugging**: 
   - Debug symbols always included (`-g -ggdb`)
   - Valgrind and GDB wrappers available in test scripts
   - Audit logs: `audit-logs/valgrind.*.log`

6. **Audio Pipelines**:
   - Configured in JSON with 4-character codec IDs (e.g., `mu08` = mulaw 8kHz, `pc44` = PCM 44.1kHz)
   - RX/TX refer to stream direction, not radio direction
   - GStreamer plugins must be installed for audio codec support

## Configuration File Locations

Priority order (later overrides earlier):
1. `/etc/rustyrig/rrserver.cfg` (system-wide)
2. `~/.config/rrserver.cfg` (user home)
3. `config/rrserver.cfg` (project directory)

Copy example files to start:
```bash
cp config/rrclient.cfg.example ~/.config/rrclient.cfg
cp config/rrserver.cfg.example ~/.config/rrserver.cfg
```

Generate TLS certificates (required for WebUI audio):
```bash
./tools/gen-selfsigned-tls-certs
```

## Common Development Tasks

### Adding a new build profile
Create `config/myprofile.config.json` based on `config/radio.config.json`, then:
```bash
PROFILE=myprofile make clean && make -j$(nproc) world
```

### Enabling/disabling features at compile time
Edit `config/radio.config.json` → modify `build.cflags` array with `-DUSE_*` flags, then rebuild.

### Working with submodules
After changing branches or pulling:
```bash
git submodule update --depth=1
```

### Cross-compiling for Windows (MSYS2)
```bash
./build.sh  # Inside MSYS2 shell; auto-detects Windows and uses cross-compile chain
```

## Codebase Maturity Notes

- Code is actively being restructured to reduce duplication
- Merger of `USE_*` and `FEATURE_*` flags is planned
- Event system is incomplete but uses json payloads
- Module lifecycle (load/unload) needs cleanup
- IRC protocol extensions for site linking in progress
