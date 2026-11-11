# Independent RustyRig libraries and Debian packaging

RustyRig's server, clients, bridges, fwdsp worker, and libfwdspmgr stay in
`PripyatAutomations/rustyrig-fw`. The public libraries are **not vendored**
and are not Git submodules of this repository.

| Upstream source | Debian development package | Runtime package |
| --- | --- | --- |
| `PripyatAutomations/librustyaxe` | `librustyaxe-dev` | `librustyaxe0` |
| `PripyatAutomations/librustyaxe` (IRC) | `librustyirc-dev` | `librustyirc0` |
| `PripyatAutomations/librrprotocol` | `librrprotocol-dev` | `librrprotocol0` |

Install the development packages (including transitive GLib/crypto
requirements) **before** building RustyRig. The main GNUmakefile probes the
installed headers and link libraries; missing dependencies cause an error,
not a Git fetch, submodule checkout, or local-library fallback.
`pkg-config` is used for optional search flags where available, while the
compiler/linker probe is authoritative. Installed runtime libraries are
resolved by the system dynamic linker.

```sh
make distclean
make -j"$(nproc)" PROFILE=rrserver
dpkg-buildpackage -us -uc -b
```

Debian's `debian/control` declares the development libraries as
`Build-Depends`. `debian/rules` does not download external dependencies and
does not build the separated libraries. Packaging continues to own the
RustyRig server/client/bridge binaries and `rustyrig-fwdsp`.

Only optional **third-party** repositories (`ext/libmongoose` and
`ext/ft8_lib`) remain submodules; neither is fetched by normal builds,
update helpers, or CI checkout. The legacy Mongoose source is initialized
only when `USE_MONGOOSE=true`, while `ext/ft8_lib` is selected with
`WITH_FT8=yes`. The regular Linux build generates `build_config.h`
without producing an EEPROM image; `USE_EEPROM=true` opts in to the
embedded-oriented packer. CI can prepare libraries from
their independent upstream repositories, install them into the build
environment, and then build this checkout against those installed SDKs.

Before distributing packages, verify SONAMEs with `readelf -d`, dependency
resolution with `dpkg-shlibdeps`, and builds using clean Devuan/Debian
chroots without network access during package creation.
