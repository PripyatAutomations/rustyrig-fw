# Distribution packaging

`packaging/PKGBUILD` is a starter Arch Linux split package. Build it with
`makepkg -f`. Run `tools/bump-version.sh --version v0.1.0` for a semantic
release (or omit `--version` while using the legacy date sequence); the script
updates `.version`, `debian/changelog`, `packaging/PKGBUILD`, and
`packaging/rustyrig.spec` together. Package metadata stores the normalized
version without the optional leading `v`.

`packaging/rustyrig.spec` is a starter Fedora/RHEL RPM spec. It is intentionally
not distribution-repo complete and has not been tested on an RPM distribution,
but gives packagers subpackages, config examples marked `noreplace`, runtime
state paths, and the systemd/SysV service files to adapt:

    rpmbuild -ba packaging/rustyrig.spec

Adjust BuildRequires and Source0 for the target distribution and release.
