#!/usr/bin/env bash
set -euo pipefail
export PATH="$PATH:/usr/sbin:/sbin"
export LC_ALL=C
cd "$(dirname "$0")/../.."
for tool in dh_installdeb fakeroot dpkg dpkg-deb; do
   if ! command -v "$tool" >/dev/null 2>&1; then
      echo "SKIP: Debian conffile tests require $tool"
      exit 0
   fi
done
work=$(mktemp -d)
cleanup() {
   local status=$?
   if (( status != 0 )); then
      for log in "$work/"*.log; do
         [[ ! -f $log ]] || tail -20 "$log" >&2
      done
   fi
   rm -rf "$work"
}
trap cleanup EXIT

# Exercise the real packaging override in an isolated, minimal staging tree.
mkdir -p "$work/source/debian" "$work/source/mk" "$work/source/packaging" "$work/source/config"
cp mk/optional-modules.mk "$work/source/mk/"
cp packaging/optional-modules.mk "$work/source/packaging/"
jq '.backend.hamlib = false' config/radio.config.json > "$work/source/config/radio.config.json"
cp debian/control debian/rules debian/rustyrig-server.maintscript "$work/source/debian/"
cp -L debian/changelog "$work/source/debian/changelog"
for package in rustyrig-server rustyrig-client rustyrig-fwdsp; do
   printf '#!/bin/sh\nexit 0\n' > "$work/source/debian/$package.preinst"
done
stage_config() {
   local package=$1 name=$2
   mkdir -p "$work/source/debian/$package/etc/rustyrig"
   printf 'packaged-default-v1\n' > "$work/source/debian/$package/etc/rustyrig/$name"
}
stage_config rustyrig-client rrclient.cfg
stage_config rustyrig-client callsign-lookup.cli.cfg
stage_config rustyrig-server rrserver.cfg
# Every required server include is a separate managed conffile.
for config in config/rrserver.*.cfg; do
   stage_config rustyrig-server "${config##*/}"
done
stage_config rustyrig-server radio.config.json
stage_config rustyrig-server ua-bans.txt
stage_config rustyrig-callsign-lookup callsign-lookup.cfg
(
   cd "$work/source"
   fakeroot make -f debian/rules override_dh_installdeb > "$work/installdeb.log" 2>&1
)
test ! -e "$work/source/debian/rustyrig-server/etc/rustyrig/callsign-lookup.srv.cfg"
for package in rustyrig-client rustyrig-server rustyrig-callsign-lookup; do
   for config in "$work/source/debian/$package/etc/rustyrig/"*; do
      grep -Fxq "/etc/rustyrig/$(basename "$config")" \
         "$work/source/debian/$package/DEBIAN/conffiles"
   done
done

# Verify the removed conffile migration is embedded in debhelper scripts.
grep -Fq "/etc/rustyrig/callsign-lookup.srv.cfg" "$work/source/debian/rustyrig-server/DEBIAN/preinst"
grep -Fq "/etc/rustyrig/callsign-lookup.srv.cfg" "$work/source/debian/rustyrig-server/DEBIAN/postinst"
# No maintainer scripts run here: dpkg only touches temporary install roots.
# Use dh_installdeb's actual conffile metadata in the fixture package.
mkdir -p "$work/package/etc/rustyrig" "$work/package/DEBIAN"
cp "$work/source/debian/rustyrig-client/DEBIAN/conffiles" "$work/conffiles"
cp "$work/source/debian/rustyrig-client/etc/rustyrig/"* "$work/package/etc/rustyrig/"
build_package() {
   local version=$1
   cat > "$work/package/DEBIAN/control" <<EOF
Package: rustyrig-client
Version: $version
Architecture: all
Maintainer: Test <test@example.invalid>
Description: Isolated conffile upgrade test
EOF
   dpkg-deb --build --root-owner-group "$work/package" "$work/client-$version.deb" >/dev/null
}
install_package() {
   local root=$1 version=$2
   mkdir -p "$root"
   dpkg --force-not-root --root="$root" --log="$root/dpkg.log" --install "$work/client-$version.deb"
}
build_package 1 # Reproduce the old package, which did not declare conffiles.
cp "$work/conffiles" "$work/package/DEBIAN/conffiles"
build_package 2
printf 'packaged-default-v3\n' > "$work/package/etc/rustyrig/rrclient.cfg"
build_package 3

for scenario in migration upgrade; do
   root="$work/$scenario"
   if [[ $scenario == migration ]]; then old=1; new=2; else old=2; new=3; fi
   install_package "$root" "$old" > "$work/$scenario-install.log" 2>&1
   printf 'my-local-settings\n' > "$root/etc/rustyrig/rrclient.cfg"
   # A blank response accepts dpkg's default, which must keep the local file.
   printf '\n' | install_package "$root" "$new" > "$work/$scenario-upgrade.log" 2>&1
   grep -Fq '[default=N]' "$work/$scenario-upgrade.log"
   grep -Fxq 'my-local-settings' "$root/etc/rustyrig/rrclient.cfg"
   test -f "$root/etc/rustyrig/rrclient.cfg.dpkg-dist"
done
grep -Fxq 'packaged-default-v1' "$work/migration/etc/rustyrig/rrclient.cfg.dpkg-dist"
grep -Fxq 'packaged-default-v3' "$work/upgrade/etc/rustyrig/rrclient.cfg.dpkg-dist"
echo 'PASS: Debian configs are conffiles; normal and legacy upgrades prompt and default to keeping local edits'
