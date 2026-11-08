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
- `librustyaxe` now has its own standalone Debian source packaging, with
  `librustyaxe0` and `librustyaxe-dev` binaries. The main tree no longer
  stages those binaries or declares them as output packages; it retains its
  in-tree development build pending the system-library compilation switch.
- `librrprotocol` is still built and packaged in-tree.

## Required work before moving the library binaries out of rustyrig-fw

### 1. Standalone upstream builds

The `librustyaxe` repository now has its own standalone `GNUmakefile` and
Debian packaging. `make deb` builds its two binary packages; `make distclean`
removes generated build and Debian staging artifacts. The package build still
needs confirmation in a clean Debian/Devuan builder. `librrprotocol` needs the
same standalone packaging work. Give each repository its own complete,
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

The `librustyaxe*` package stanzas and staging recipes have been removed
from `rustyrig-fw`; its packages now depend on `librustyaxe0`, without a
cross-source `${binary:Version}` pin. Before release, test the external library
build and binary upgrades. Remove `librrprotocol*` from the main source only
after its own package and external build path are ready. Rename runtime
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

## librustyaxe provider selection

Ordinary builds now choose the installed `librustyaxe` pkg-config package first.
Set `LIBRUSTYAXE_PROVIDER=system` to require it, or
`LIBRUSTYAXE_PROVIDER=local` to build from the checked-out submodule.
Auto mode initializes that submodule when its files are absent. Debian,
Arch, and RPM builds require the installed library; they must not package
another copy. `librrprotocol` remains built in-tree pending its separate
source-package transition.

The selected provider supplies matching headers and linker inputs. No
checkout-root symlink is generated for a system library; source-build tests
and the callsign lookup sub-build follow the same provider mode. When
switching from a local build to system mode, run `make distclean` first to
remove old root-level shared objects that could shadow the installed SONAME.
