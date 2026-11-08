# Debian source/package split plan

RustyRig's main development checkout remains a **single tree** with submodules,
but the Debian archive should build three independent source packages. Keep
these concepts separate:

| Debian source | Git origin | Debian binary packages |
| --- | --- | --- |
| `librustyaxe` | `PripyatAutomations/librustyaxe` | `librustyaxe0`, `librustyaxe-dev` |
| `librrprotocol` | `PripyatAutomations/librrprotocol` | `librrprotocol0`, `librrprotocol-dev` |
| `rustyrig-fw` | `PripyatAutomations/rustyrig-fw` | `rustyrig-common`, `rustyrig-server`, `rustyrig-client`, `rustyrig-client-gtk`, `rustyrig-bridge`, `rustyrig-fwdsp`, `librustyirc` / `libfwdspmgr` runtime + dev packages, and optional module packages |

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
- The rest of the existing source and packaging still builds the two libraries
  **in-tree**. No conflicting partial switch to external libraries has occurred.

## Required work before moving the library binaries out of rustyrig-fw

### 1. Standalone upstream builds

Both library repositories currently contain a `GNUmakefile` symlink pointing
to `../rrclient/GNUmakefile`. They therefore cannot be independently built
from their own source packages. Give each repository its own complete,
versioned standalone build, with `make`, `make check`, and DESTDIR installation.

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
3. Build `rustyrig-fw` with both installed -dev packages.

Version dependencies must follow library ABI compatibility and
`dh_makeshlibs`/`dh_shlibdeps`, **not**
`(= ${binary:Version})` across separately versioned Debian sources.

### 3. RustyRig external-library build

Introduce and test an explicit mode such as
`USE_SYSTEM_RUSTYRIG_LIBS=1` *after* the upstream library packages are
buildable. That mode must not include `librustyaxe/rules.mk` or
`librrprotocol/rules.mk`, must find installed headers and linker libraries
(and not accidentally prefer Git-submodule headers), must not demand local
submodule `*.h` as make prerequisites, and must not stage either external
library in `debian/rules`.

A clean Debian orig tarball for this mode must omit those submodule sources.
Preserve a separate in-tree developer build workflow.

### 4. Transition

Remove the `librustyaxe*` and `librrprotocol*` binary package stanzas from
`rustyrig-fw/debian/control` only after the separate source packages are
available and its external-library mode passes a clean sbuild. Rename runtime
library packages based on verified SONAMEs. Avoid file-overlap in upgrades from
the current combined source package and confirm all binary package dependency
relations with dpkg-shlibdeps.

`librustyirc` and `libfwdspmgr` can remain binary packages of the main
RustyRig source. `rrbridge`, `rrserver`, and `rrclient` should stay in the
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
