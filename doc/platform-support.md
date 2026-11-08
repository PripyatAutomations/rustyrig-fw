# First-class platform and build policy

RustyRig should remain compiler-first and package-manager-neutral.  A binary
build must not require Debian/Ubuntu/Devuan tooling or a particular init
system.  Packaging is a separate step.  The long-term supported targets are:

| Target | Distribution / build form | Acceptance requirements |
| --- | --- | --- |
| Debian family | Debian, Devuan, Ubuntu, derivatives | Native + source .deb; no systemd dependency in core binaries; alternative init scripts |
| Red Hat family | Fedora, RHEL and compatible distros | RPM source and binary builds, correct libdir and dependency metadata |
| Arch Linux | Arch and derivatives | PKGBUILD with pacman-owned dependencies, no Debian tools |
| Windows x64 | Native Windows executables (prefer MinGW-w64 cross compilation) | PE/COFF DLL/import libraries, explicit ABI exports, TLS/network portability |
| macOS | Native macOS builds | Mach-O dylibs/install names, clang, Homebrew/MacPorts paths |
| ESP32 | Future rrserver-only target | ESP-IDF toolchain, selective source sets, no POSIX-only GUI/IPC modules |
| Generic Linux | Source build with usable compiler/development libraries | Native compiler/linker search paths plus optional pkg-config, PREFIX/DESTDIR and uninstall |

## Build boundaries

The `librustyaxe` repository owns MIT `librustyaxe` and MIT
`librustyirc`, compiled as independent shared libraries. `librustyirc`
depends on `librustyaxe`; the latter merely recommends the IRC runtime
in Debian packaging. `librrprotocol` remains a separate GPLv2 upstream.

Installed compiler-usable libraries take precedence; a Git submodule is a
convenience for running uninstalled source-tree tests, never an implicit
installer. Build dependency probes must honor CC, CPPFLAGS, CFLAGS,
LDFLAGS, configured sysroots and cross-compilers and must not assume a
Linux-native execution environment.

Debian `make deb` and `make src-deb` are opt-in packaging targets, not
requirements of `make`. Arch/RPM builds require their corresponding external
libraries and must not repackage copies. Cross-target builds must use target
specific libraries and headers, never accidentally link host libraries.

## Installation and removal

Every source-installable component should expose `make install` and
`make uninstall`, honoring PREFIX, platform library/module directories
and DESTDIR. Uninstall may only remove files installed by that component.
Never remove operator-managed configurations, databases, recordings,
logs, installed shared dependencies or package-manager-owned files.
Prefer using the native package manager to uninstall when binaries were
installed through Debian/RPM/pacman. Source `make uninstall` is intended
for corresponding `make install` runs, not packaged installations.

## Current gaps before release certification

The portable compiler probes and library-ownership split are in progress;
they do not constitute tested Windows/macOS/ESP32 support. Audit remaining
ELF-only flags (SONAME/readelf), Unix-only source files, runtime loader
paths, Windows DLL exports and source-install layout for each target.
Require CI build jobs for each supported toolchain and at least one
clean package build + install/uninstall test for each package ecosystem.
