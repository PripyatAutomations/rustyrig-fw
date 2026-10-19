# Release candidate checklist

This is a **human-run checklist**, not an automated claim that a release has passed. Use it for each RC and before promoting a build to stable.

## Before tagging

- [ ] Choose a version and ensure `.version`, `CHANGELOG` / `debian/changelog`, Arch/RPM metadata and package version all agree. Use `tools/bump-version.sh --version v1.0-rc1` after reviewing its changes; do not edit just one version file.
- [ ] Record the Git commit and submodule revisions. Check `git status --short` and `git submodule status --recursive`; avoid testing against moving submodule branches.
- [ ] Confirm the build profile and optional SDK flags, including which optional binary packages should be produced.
- [ ] Build from a clean checkout on a supported Debian-family system, including GTK client, server, audio worker and needed modules.
- [ ] Run available tests and record any deliberately skipped test suites. Debian packaging currently skips optional hardware/integration tests: package success alone is not a full test.
- [ ] Check for accidentally tracked generated files (especially `debian/files`, `debian/.debhelper`, `debian/tmp`, build outputs and database files).

## Package the release

```sh
git status --short
git submodule status --recursive
make -j"$(nproc)" PROFILE=radio world
make PROFILE=radio debs
```

Review package names and versions from the parent directory before installing. Install a **matched set** of built shared libraries, server/client and DSP packages rather than mixing release candidates. For a specific package file, `dpkg-deb --info ./package.deb` and `dpkg-deb --contents ./package.deb` help inspect its metadata and paths.

Confirm Debian service/configuration behavior on a disposable VM or test host:

- [ ] Fresh install provisions the server account, configuration, database and admin bootstrap credentials correctly.
- [ ] Upgrade preserves the existing database and locally edited conffiles; no config or user data is unexpectedly replaced.
- [ ] Package remove, reinstall and upgrade leave ownership and permissions sensible.
- [ ] GTK launcher/icon and optional module dependencies are correct.
- [ ] Test service start/stop under the target init system; do not assume a `systemctl` command covers Devuan/OpenRC/SysV hosts.
- [ ] Confirm the packaged WebUI loads and its revision matches the server's protocol.

## Smoke test with real processes

- [ ] Start `rrserver`, connect GTK and TUI clients, and authenticate with a normal account.
- [ ] Join/part a station and rig room, verify VFO controls and room list after restart.
- [ ] Tune a test rig and receive/decode audio; test TX/PTT only on an authorized safe test station.
- [ ] Verify independent audio routing, codec switching and PTT authorization across two clients.
- [ ] Check server links with matching RC builds; note which mixed-version combinations were not tested.
- [ ] Connect an SDR from SDR Directory; verify its selected `#sdr-*` room has working RX controls and **does not create a duplicate `-rigN` room**.
- [ ] Verify RX-only VFOs hide TX controls, and floating/docked GTK widgets remain usable.
- [ ] Test full-screen, window resize/zoom, and Ctrl+0/Alt+0 key behavior.
- [ ] Restart server/client to catch stale state, room cleanup and DB migration problems.
- [ ] Record logs, regressions and test host details for each RC.

## Publish

- [ ] Prepare concise release notes: key improvements, supported platforms, upgrade instructions, known issues and tested combinations.
- [ ] Tag the **tested** commit and publish artifacts built from that same commit.
- [ ] Attach checksums for distributable files and retain a copy of the exact source/submodule revisions.
- [ ] Label preview builds clearly as RCs. Only publish the stable tag after a successful final candidate and a separate upgrade smoke test.

## Stop-ship conditions

Do not promote a build if it loses user configuration or databases, breaks authentication/authorization, can leave TX keyed after disconnect, installs mismatched libraries/modules, or has a reproducible crash during ordinary first-run operation.
