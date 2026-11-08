# Debian archive readiness review (2026-11-07)

This checklist targets **official Debian** (Debian mentors/sponsorship and NEW
processing), not merely generating locally installable .deb files. A working
package is not automatically an uploadable source package.

## Current blockers — resolve before requesting a sponsor

- [ ] **Complete `debian/copyright`** in machine-readable format (DEP-5 is
  recommended). The file is currently absent. Include accurate upstream
  ownership and licenses for all distributed sources and compiled assets;
  `doc/LICENSE` covers only RustyRig GPL-2.0-only. `librustyaxe` remains
  MIT, `librrprotocol` is GPLv2, and all other submodules, Windows GTK themes,
  icon artwork, and bundled source must retain their actual upstream
  copyrights/licenses. Do not declare whole trees GPLv2 blindly.
- [ ] **Use non-native Debian packaging**: `debian/source/format` currently
  says `3.0 (native)` and `debian/changelog` versions have no Debian
  revision. Prefer `3.0 (quilt)` and e.g. `1.0.2-1` for the initial
  downstream packaging revision. Coordinate with `tools/bump-version.sh` and
  the source tarball naming before switching; do not break RustyRig's
  upstream release process.
- [ ] **Make the source tarball self-contained.** The current build requires
  Git submodules including `librustyaxe`, `librrprotocol`,
  `callsign-lookup`, and optional `ext/ft8_lib` / `ext/libmongoose`.
  `dpkg-buildpackage` and a Debian build daemon must not have to clone or
  fetch anything from the network. Decide which dependencies to package
  separately and which (if legally and technically appropriate) to include
  in a generated, audited upstream orig tarball.
- [ ] **Verify shared library ABI and naming.** Four binary library packages
  currently have unversioned names (`librustyaxe`, `librustyirc`,
  `librrprotocol`, `libfwdspmgr`) although libraries are staged as
  `*.so.0`. Verify actual `readelf -d` SONAMEs, versioned Debian package
  names, `dh_makeshlibs`/`dh_shlibdeps` output, runtime dependencies and
  correct `-dev` links. Ensure ABI changes lead to appropriate transitions.
- [ ] **Validate licensing of every dependency actually shipped.** The
  RustyRig project has migrated to GPL-2.0-only. Confirm license
  compatibility and source availability for optional FT8, Mongoose and
  other code without modifying upstream copyright notices.

## Build/install quality issues

- [x] Maintainer scripts now include `#DEBHELPER#` so debhelper inserts
  generated init-system, trigger and conffile-migration snippets.
- [ ] `override_dh_auto_test` currently suppresses all tests. Run a
  deterministic network-/hardware-independent subset in package builds;
  keep online, rig-hardware and audio-device integration tests optional.
- [ ] Confirm `libhamlib-dev <pkg.rustyrig.hamlib>` build profile behavior
  with clean build-dependency resolution. A normal Debian archive build
  must not silently omit the intended hamlib backend.
- [ ] Check the automatic initialization flow under noninteractive debconf.
  New databases currently require an admin password and will cause postinst
  to fail if no password was provided. Plan a secure, unattended initial
  install strategy, without leaving enabled default credentials.
- [ ] Examine account provisioning (preinst vs postinst / sysusers), file
  ownership, ACLs, `/run` versus persistent directories, and maintainer
  script behavior under remove/purge/failed upgrades. Avoid resetting
  user data and avoid changing shared directory ownership on upgrades.
- [ ] Check `dh_missing --fail-missing`, installed-file ownership across
  binary packages, and duplicated documentation. Preserve SysV init support
  for Devuan while testing the Debian systemd unit.
- [ ] The generated `/etc/rustyrig/rrclient.css` is a **reference document**,
  not a config input. Confirm its placement and dpkg-conffile upgrade
  semantics are intentional; Debian normally puts examples/reference
  material under `/usr/share/doc/PACKAGE/examples`.
- [ ] Verify library hardening/build flags, `SOURCE_DATE_EPOCH`,
  reproducibility and architecture coverage, especially optional modules.
- [ ] Add `debian/watch` when upstream release artifacts are available.
  Provide an ITP/RFP bug reference and review existing Debian package
  namespaces for conflicts.

## Verification for a candidate upload

In a **clean**, fully dependency-populated Debian environment (not on the
developer's running server):

```sh
dpkg-buildpackage -S -us -uc
sbuild --no-clean-source --arch-all
lintian -i ../*.changes
# Use a disposable VM/container for full install/upgrade/purge checks.
```

Also inspect:

```sh
dpkg-deb --contents ../rustyrig-*.deb
dpkg-deb --info ../rustyrig-*.deb
readelf -d debian/librrprotocol/usr/lib/*/librrprotocol.so.0 | grep SONAME
```

Test installation and removal on **Debian stable/testing and Devuan**,
including service startup disabled/unconfigured, upgrades with edited
conffiles, dependencies between split packages, and both GTK and TUI.

For Debian sponsorship guidance see:
https://mentors.debian.net/intro-maintainers/
For Debian Policy see:
https://www.debian.org/doc/debian-policy/

**This is a source review only**; no `dpkg-buildpackage`, `sbuild`,
`lintian`, or installation/upgrade tests were executed during this review.
