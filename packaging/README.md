# Distribution packaging

`packaging/PKGBUILD` is a starter Arch Linux split package. Build it with
`makepkg -f`. Run `tools/bump-version.sh --version v0.1.0` for a semantic
release (or omit `--version` while using the legacy date sequence); the script
updates `.version`, the root `CHANGELOG`, `packaging/PKGBUILD`, and
`packaging/rustyrig.spec` together. `debian/changelog` is a symlink to the
root changelog, so Debian consumes that same release history. Package metadata
stores the normalized version without the optional leading `v`.

`packaging/rustyrig.spec` is a starter Fedora/RHEL RPM spec. It is intentionally
not distribution-repo complete and has not been tested on an RPM distribution,
but gives packagers subpackages, config examples marked `noreplace`, runtime
state paths, and the systemd/SysV service files to adapt:

    rpmbuild -ba packaging/rustyrig.spec

Adjust BuildRequires and Source0 for the target distribution and release.

## Loadable modules

Server modules install under `/usr/lib/rustyrig/modules/rrserver`; client
modules under `/usr/lib/rustyrig/modules/rrclient`. Both directories contain
root-owned executable code. Runtime data remains under `/var/lib/rustyrig`.
`path.modules` in each program's config/defaults selects its own directory.
Source-tree runs can explicitly set `path.modules=./bin`.

`rustyrig-server-gpsd` is an optional separate Debian, Arch and RPM package containing
`rrserver-gpsd.so`. It depends on the server package; enable `rrserver-gpsd`
in `[modules]` and configure `gpsd.url`/`gpsd.target`. The NMEA adapter remains
in the server package. GTK remains in `rustyrig-client-gtk`.

Package managers preserve modified configuration. On upgrade, update existing
`path.modules` values from `/var/lib/rustyrig/modules` to the appropriate new
directory. Do not make the module directories writable by the service account.
`doc/*.cfg.example` are exact copies of the shipped `config/*.cfg` files.
