#!/usr/bin/env bash

set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

usage() {
   cat <<'EOF'
Usage: tools/bump-version.sh [--version VERSION] [--dry-run]

Advance the RustyRig package version and keep the package metadata in sync.
VERSION may be a legacy YYYYMMDD.NN value or a semantic version such as
0.1.0 (an optional leading 'v' is accepted). Without --version, a legacy
date sequence is incremented, or the semantic patch component is incremented.
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

normalize_version() {
   local version=$1
   version=${version#v}
   if [[ "$version" =~ ^[0-9]{8}\.[0-9]+$ ]]; then
      printf '%s' "$version"
   elif [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
      printf '%s' "$version"
   else
      return 1
   fi
}

if [[ -n "$requested_version" ]]; then
   new_version=$(normalize_version "$requested_version") || {
      echo "invalid version: $requested_version (expected YYYYMMDD.NN or MAJOR.MINOR.PATCH)" >&2
      exit 2
   }
else
   if [[ "$old_version" =~ ^[0-9]{8}\.[0-9]+$ ]]; then
      version_date=$(date +%Y%m%d)
      old_date=${old_version%%.*}
      old_sequence=${old_version#*.}
      if [[ "$old_date" == "$version_date" ]]; then
         next_sequence=$((10#$old_sequence + 1))
      else
         next_sequence=1
      fi
      new_version=$(printf '%s.%02d' "$version_date" "$next_sequence")
   elif [[ "$old_version" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
      major=${BASH_REMATCH[1]}
      minor=${BASH_REMATCH[2]}
      patch=${BASH_REMATCH[3]}
      new_version="${major}.${minor}.$((10#$patch + 1))"
   else
      version_date=$(date +%Y%m%d)
      new_version="${version_date}.01"
   fi
fi

echo "VERSION: ${old_version:-unset} -> $new_version"

if $dry_run; then
   exit 0
fi

printf '%s\n' "$new_version" > .version

if [[ -f CHANGELOG ]]; then
   sed -i -E "1s/^rustyrig-fw \([^)]*\)/rustyrig-fw (${new_version})/" CHANGELOG
fi

if [[ -f packaging/PKGBUILD ]]; then
   sed -i -E "s/^pkgver=.*/pkgver=${new_version}/" packaging/PKGBUILD
fi

if [[ -f packaging/rustyrig.spec ]]; then
   sed -i -E "s/^Version:[[:space:]].*/Version: ${new_version}/" packaging/rustyrig.spec
fi

echo "Updated .version, CHANGELOG, Arch, and RPM package metadata."
