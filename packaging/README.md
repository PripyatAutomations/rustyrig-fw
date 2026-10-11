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

Debian registers every packaged file under `/etc` as a conffile through
`dh_installdeb`. Upgrades prompt when both the local configuration and the
packaged default have changed; the default answer keeps the local file and
saves the new default as `.dpkg-dist`. The first fixed package also prompts
for locally changed files installed by older packages without conffile metadata.
An unchanged packaged default needs no prompt. Explicit dpkg/apt force-conf
options can override this behavior.

Package managers preserve modified configuration. On upgrade, update existing
`path.modules` values from `/var/lib/rustyrig/modules` to the appropriate new
directory. Do not make the module directories writable by the service account.
`doc/*.cfg.example` are exact copies of the shipped `config/*.cfg` files.

Debian client/server packages create `rradmin` and ask for existing local
usernames to add (space-separated; empty adds nobody). Existing members are
retained; log in again to activate new membership. Reconfigure either package
with `dpkg-reconfigure` to change the requested additions. State remains under
`/var/lib/rustyrig` and logs under `/var/log/rustyrig`; `/var/lib/rustyrig/db`
is the FHS location for application databases, rather than BSD's `/var/db`.
Package setup makes existing directories listable/traversable and removes
public file access. Access and default ACLs grant the `rustyrig` and `rradmin`
groups maintenance access, including SQLite journal/WAL files and new logs.
No database reset or service ownership change is performed.

Installed rrclient loads `/etc/rustyrig/rrclient.cfg` and then
`~/.config/rrclient.cfg`; use the latter for personal credentials and overrides.
The GTK server editor saves changed values only to that user file (mode 0600).
`tools/test-client.sh`, `tools/test-server.sh` and `tools/test-all.sh` prefer
source libraries/binaries, require local shared libraries, and generate local
configs in `run/`. Database, log, recording and module paths point inside the
source tree. These launchers work from other working directories and never
stop unrelated installed services. Explicit `-f` bypasses configuration layering.
