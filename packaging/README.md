# Distribution packaging

`packaging/PKGBUILD` is a starter Arch Linux split package. Build it with
`makepkg -f`; update `pkgver` and the source revision for a release.

`packaging/rustyrig.spec` is a starter Fedora/RHEL RPM spec. It is intentionally
not distribution-repo complete and has not been tested on an RPM distribution,
but gives packagers subpackages, config examples marked `noreplace`, runtime
state paths, and the systemd/SysV service files to adapt:

    rpmbuild -ba packaging/rustyrig.spec

Adjust BuildRequires and Source0 for the target distribution and release.
