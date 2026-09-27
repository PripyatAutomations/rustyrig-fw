#!/usr/bin/env bash

set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

usage() {
   cat <<'EOF'
Usage: tools/bump-version.sh [--version YYYYMMDD.NN] [--dry-run]

Advance the RustyRig package version and keep the package metadata in sync.
Without --version, the sequence is incremented for today's date.
EOF
}

requested_version=""
dry_run=false
while (($#)); do
   case "$1" in
      --version)
         (($# >= 2)) || { echo "--version requires a value" >&2; exit 2; }
         requested_version=$2
         shift 2
         ;;
      --dry-run)
         dry_run=true
         shift
         ;;
      -h|--help)
         usage
         exit 0
         ;;
      *)
         echo "unknown option: $1" >&2
         usage >&2
         exit 2
         ;;
   esac
done

old_version=$(tr -d '[:space:]' < .version 2>/dev/null || true)
if [[ -n "$requested_version" ]]; then
   [[ "$requested_version" =~ ^[0-9]{8}\.[0-9]{2,}$ ]] || {
      echo "invalid version: $requested_version (expected YYYYMMDD.NN)" >&2
      exit 2
   }
   new_version=$requested_version
else
   version_date=$(date +%Y%m%d)
   old_date=${old_version%%.*}
   old_sequence=${old_version#*.}
   if [[ "$old_date" == "$version_date" && "$old_sequence" =~ ^[0-9]+$ ]]; then
      next_sequence=$((10#$old_sequence + 1))
   else
      next_sequence=1
   fi
   new_version=$(printf '%s.%02d' "$version_date" "$next_sequence")
fi

echo "VERSION: ${old_version:-unset} -> $new_version"

if $dry_run; then
   exit 0
fi

printf '%s\n' "$new_version" > .version

if [[ -f debian/changelog ]]; then
   sed -i -E "1s/^rustyrig-fw \([^)]*\)/rustyrig-fw (${new_version})/" debian/changelog
fi

if [[ -f packaging/PKGBUILD ]]; then
   sed -i -E "s/^pkgver=.*/pkgver=${new_version}/" packaging/PKGBUILD
fi

if [[ -f packaging/rustyrig.spec ]]; then
   sed -i -E "s/^Version:[[:space:]].*/Version: ${new_version}/" packaging/rustyrig.spec
fi

echo "Updated .version, Debian, Arch, and RPM package metadata."
