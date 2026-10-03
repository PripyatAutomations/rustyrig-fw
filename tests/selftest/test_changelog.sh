#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ ! -L debian/changelog ]]; then
   echo "FAIL: debian/changelog must be a symlink" >&2
   exit 1
fi

if [[ "$(readlink debian/changelog)" != "../CHANGELOG" ]]; then
   echo "FAIL: debian/changelog must point to ../CHANGELOG" >&2
   exit 1
fi

if command -v dpkg-parsechangelog >/dev/null 2>&1; then
   dpkg-parsechangelog -l debian/changelog >/dev/null
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/tools" "$work/debian"
cp tools/bump-version.sh "$work/tools/"
cp CHANGELOG "$work/"
printf '%s\n' '20261003.01' > "$work/.version"
ln -s ../CHANGELOG "$work/debian/changelog"
bash "$work/tools/bump-version.sh" --version 1.2.3 >/dev/null

if [[ ! -L "$work/debian/changelog" ]] ||
   ! grep -q '^rustyrig-fw (1\.2\.3) ' "$work/CHANGELOG"; then
   echo "FAIL: version bump did not preserve the changelog symlink" >&2
   exit 1
fi

echo "PASS: Debian uses the parseable root CHANGELOG and version bumps preserve the symlink"
