# Preparing a RustyRig release candidate

Release candidates are usable test builds, not promises of perfect behavior. The minimum bar is simple: the selected build profile compiles, packages install with matching libraries/modules, an existing installation upgrades without losing data, and an operator can connect and use a basic station.

Use [the release checklist](release-checklist.md) for smoke tests. Keep UI testing and packaging testing separate so a GUI regression does not conceal a packaging failure.

## 1. Choose and verify the release version

RustyRig currently has several version sources:

- `.version` for the source build
- `CHANGELOG` (also `debian/changelog`) for Debian
- `packaging/PKGBUILD` and `packaging/rustyrig.spec` for other distributions

**These sources currently need reconciliation.** Run:

```sh
bash tools/release-preflight.sh
head -n 1 CHANGELOG
cat .version
```

Use `tools/bump-version.sh` to update **all formats from `.version`**. For example:

```sh
tools/bump-version.sh --version v1.0.0-rc1 --dry-run
tools/bump-version.sh --version v1.0.0-rc1
bash tools/release-preflight.sh
```

The script writes the canonical `1.0.0-rc1` into `.version`, prepends a Debian changelog entry with `1.0.0~rc1`, writes `1.0.0_rc1` into Arch `pkgver`, and uses RPM `Version: 1.0.0` with `Release: 0.rc1`. Existing changelog history remains intact. The user-facing Git tag can use `v1.0-rc1` if desired; the canonical package version is three-part.

Do not manually change `debian/changelog`, `packaging/PKGBUILD`, or the RPM spec to a different version. The preflight warns about mismatches. Review the generated changelog text and fill in the actual changes before release.

## 2. Record the source

```sh
git status --short
git submodule status --recursive
git rev-parse HEAD
bash tools/release-preflight.sh
```

Record the commit and submodule SHAs in release notes. Avoid updating submodules from their remote branches between testing and packaging.

## 3. Build and inspect the Debian packages

On the target Debian-family build host, with required SDKs installed:

```sh
make -j"$(nproc)" PROFILE=radio world
make PROFILE=radio debs
```

Built `.deb` files normally appear in the parent directory. Inspect names, versions, file paths and dependencies before installing:

```sh
dpkg-deb --info ../rustyrig-server_*.deb
dpkg-deb --contents ../rustyrig-server_*.deb
```

Install the appropriate server, client, GTK and DSP packages plus their matching library packages together. Do not install a package glob blindly if it matches older builds. Optional modules are separate packages; the build profile and available SDKs determine which ones are built.

Debian's `debian/rules` currently skips the optional hardware/integration test suite. A successful `.deb` build is **not** proof that the hardware or UI was tested.

## 4. Check installation and upgrades

Use a disposable VM or test server with a copy of an existing database and locally modified configuration. A fresh install should provision users, state directories, package-owned modules, and bootstrap credentials as documented. An upgrade should preserve the database and locally modified conffiles.

Check both systemd and the relevant SysV/OpenRC service paths if publishing for Devuan. Avoid advertising `systemctl` as the only way to manage the server.

Only after verifying an upgrade should you test the assembled packages with a real server/client and, where available, a connected rig or SDR.

## 5. Publish a candidate

Tag the exact tested source commit, publish packages from that commit, and provide checksums. Release notes should say what was tested, what changed, and which known issues remain. Every candidate should have its own version and clear upgrade path.

Before the final stable release, verify the stable package sorts *newer* than the last RC using `dpkg --compare-versions`. Avoid deleting or recreating an operator database as part of an upgrade.

## Recommended stop-ship issues

Hold a release for package build failures, failed startup/login, destructive upgrades, mismatched shared libraries or loadable modules, or unsafe TX behavior. Small UI glitches and clearly documented experimental limitations may be acceptable in an RC.
