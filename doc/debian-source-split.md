# Debian source/package split plan

RustyRig's main development checkout remains a **single tree** with submodules,
but the Debian archive should build four independent source packages. Keep
these concepts separate:

| Debian source | Git origin | Debian binary packages |
| --- | --- | --- |
| `librustyaxe` | `PripyatAutomations/librustyaxe` | `librustyaxe0`, `librustyaxe-dev`, `librustyirc0`, `librustyirc-dev` |
| `librrprotocol` | `PripyatAutomations/librrprotocol` | `librrprotocol0`, `librrprotocol-dev` |
| `rustyrig-callsign-lookup` | `PripyatAutomations/callsign-lookup` | `rustyrig-callsign-lookup` |
| `rustyrig-fw` | `PripyatAutomations/rustyrig-fw` | `rustyrig-common`, `rustyrig-server`, `rustyrig-client`, `rustyrig-client-gtk`, `rustyrig-bridge`, `rustyrig-fwdsp`, ``libfwdspmgr` runtime + dev packages, and optional module packages |

**The library-package names ending in `0` are provisional.** Confirm actual
ELF SONAMEs and ABI policy before publishing; do not rename just because the
source build emits `*.so.0`.

## Implemented in the main tree

- `rustyrig-common` owns `/usr/lib/rustyrig/admin-permissions`, previously
  misplaced in the `librustyaxe` Debian binary package. Both server and client
  depend on this common binary package.
- `rustyrig-bridge` owns `/usr/bin/rrbridge` and
  `/usr/lib/rustyrig/modules/rrbridge/rrbridge-kiwi.so`. The server recommends
  this optional bridge.
- `librustyaxe` now has its own standalone Debian source packaging, with
  `librustyaxe0` and `librustyaxe-dev` binaries. The main tree no longer
  stages those binaries or declares them as output packages; it retains its
  in-tree development build pending the system-library compilation switch.
- `librrprotocol` now has its own standalone source build, `librrprotocol0`
  and `librrprotocol-dev` packages, plus source-package support. The main
  RustyRig Debian packaging no longer installs its shared library or headers.

## Required work before moving the library binaries out of rustyrig-fw

### 1. Standalone upstream builds

The `librustyaxe` repository now has its own standalone `GNUmakefile` and
Debian packaging. `make deb` builds its two binary packages; `make distclean`
removes generated build and Debian staging artifacts. The package build still
needs confirmation in a clean Debian/Devuan builder. `librrprotocol` now has
standalone packaging and DESTDIR installation as well. Both library repositories
provide `make deb` (binary packages) and `make src-deb` (Debian source
package with an upstream Git archive); `make distclean` removes build and
package staging files. Verify these commands in a clean Debian/Devuan build
before releasing.

The new build must produce a versioned real shared library, SONAME symlink,
development linker symlink, public headers, and pkg-config metadata. Make
module paths, flags, and include ordering work without the RustyRig parent
checkout or an Internet connection.

The RustyRig checkout can continue including the library sources as submodules
for development, and can use explicit in-tree mode.

### 2. Release/dependency order

1. Publish/build/install `librustyaxe0` and `librustyaxe-dev` (MIT).
2. Build `librrprotocol` against the installed `librustyaxe-dev` package.
   Publish/install `librrprotocol0` and `librrprotocol-dev` (GPLv2).
3. Build `rustyrig-callsign-lookup` independently against `librustyaxe-dev`.
4. Build `rustyrig-fw` against the installed libraries; the callsign lookup
   helper is an optional runtime package, not an in-tree build prerequisite.

Version dependencies must follow library ABI compatibility and
`dh_makeshlibs`/`dh_shlibdeps`, **not**
`(= ${binary:Version})` across separately versioned Debian sources.

### 3. RustyRig external-library build

Ordinary builds select each library using a compiler/linker probe. A usable
installed library takes precedence even without a pkg-config file or Debian
packaging. The optional pkg-config metadata only supplies additional include
and link search paths. `LIBRUSTYAXE_PROVIDER` and `LIBRRPROTOCOL_PROVIDER`
can each be `auto`, `system`, or `local`. Protocol system mode requires
installed RustyAxe to avoid mixing incompatible library ABIs. An uninstalled
developer build uses the checkout submodules. Debian packaging forces system
mode, and does not stage either external library.

A clean Debian orig tarball for this mode must omit those submodule sources.
Preserve a separate in-tree developer build workflow.

### 4. Transition

The `librustyaxe*`, `librustyirc*` and `librrprotocol*` binary package stanzas and staging
recipes have been removed from `rustyrig-fw`; its packages depend on the
external `librustyaxe0` and `librrprotocol0` libraries. Before release,
validate clean builds and upgrades. Rename runtime packages based on verified
SONAMEs. Avoid file-overlap in upgrades from
the current combined source package and confirm all binary package dependency
relations with dpkg-shlibdeps.

`librustyirc` now builds as a separate GPLv2 library within the RustyAxe
repository; its runtime depends on RustyAxe and RustyAxe recommends IRC.
`libfwdspmgr` remains in the RustyRig source. `rrbridge`, `rrserver`, and `rrclient` should stay in the
same Git checkout and Debian source unless there's a separate reason to change
their release cadence.

## Packaging acceptance checks

- No Git fetch, submodule initialization or dependency download in
  `debian/rules`, even with `HOME` and network access restricted.
- `dpkg-buildpackage -S`, clean `sbuild`, `lintian`, and a fresh install,
  upgrade and purge pass on Debian and Devuan.
- `dpkg -S /usr/bin/rrbridge` resolves to `rustyrig-bridge`.
- `dpkg -S /usr/lib/rustyrig/admin-permissions` resolves to
  `rustyrig-common`.
- All three source packages have accurate `debian/copyright` entries;
  third-party assets keep their original licenses.
- Run `readelf -d` and `dpkg-shlibdeps` on each shared library to confirm
  SONAME and package dependencies.

Reference: https://www.debian.org/doc/debian-policy/ch-sharedlibs.html

## Library provider selection (non-Debian builds)

Normal `make` selects an installed, compiler-linkable copy of `librustyaxe`
and `librrprotocol` before using a Git submodule. Neither library has to be
installed through Debian; custom compiler include and library paths work, and
pkg-config is optional for discovery. The submodules remain available for
temporary uninstalled testing. No automatic upgrade of installed libraries
occurs; update/install each upstream library separately when desired.

Normal source-tree builds can use the local submodules without installing them.
The top-level `make install` instead requires both library providers to be
`system`: RustyRig never permanently installs `librustyaxe` or
`librrprotocol` from its fallback submodules. `make install-dev` likewise
installs only the development files still owned by RustyRig.

Force a mode with `LIBRUSTYAXE_PROVIDER=system|local` and
`LIBRRPROTOCOL_PROVIDER=system|local`. An old checkout-root SONAME must be
removed with `make distclean` before switching to a system provider.
Standalone library source packages are created with `make src-deb`, which
requires a clean Git working tree and writes the `.dsc`, orig tarball and
Debian packaging artifacts beside the repository. `make deb` builds each
library's runtime and development binaries. Source-package generation uses
`dpkg-buildpackage` only for the packaging target; ordinary `make` does not
need Debian tools.

`librustyirc` is maintained in `librustyaxe/librustyirc/`, while remaining
a distinct MIT shared library and separate Debian runtime/development
binary pair. RustyRig uses an installed IRC library by default and falls back
to the RustyAxe submodule for uninstalled development.

## Standalone callsign-lookup

The callsign helper is no longer a RustyRig Git submodule, source build target,
or binary package of the main `rustyrig-fw` Debian source. Its own repository
owns `/usr/bin/callsign-lookup` and `/etc/rustyrig/callsign-lookup.cfg`, with
`make deb` and `make src-deb`. Its QRZ endpoint uses libcurl HTTPS, so it
requires neither Mongoose nor wslay: wslay is only needed for WebSockets.
