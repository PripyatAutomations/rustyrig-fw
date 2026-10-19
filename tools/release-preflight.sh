#!/usr/bin/env bash
# Read-only preflight for release candidates. Does not bump, commit or tag.
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0
note() { printf '%s\n' "$*"; }
warn() { note "WARN: $*"; }
bad() { note "FAIL: $*"; fail=1; }
note "RustyRig release preflight (read-only)"
for cmd in git jq; do command -v "$cmd" >/dev/null || bad "Missing $cmd"; done
[[ -f .version ]] || bad "Missing .version"
if [[ -f .version ]]; then
  version=$(tr -d '[:space:]' < .version)
  note "Current version: $version"
  expected_debian="$version"
  expected_arch="$version"
  expected_rpm="$version"
  expected_rpm_release='1%{?dist}'
  if [[ "$version" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-rc([0-9]+)$ ]]; then
    base="${BASH_REMATCH[1]}"
    rc="${BASH_REMATCH[2]}"
    expected_debian="${base}~rc${rc}"
    expected_arch="${base}_rc${rc}"
    expected_rpm="$base"
    expected_rpm_release="0.rc${rc}%{?dist}"
  fi
  if [[ -f CHANGELOG ]]; then
    header=$(head -n 1 CHANGELOG)
    note "$header"
    changelog_version=$(sed -n '1s/^rustyrig-fw (\\([^)]*\\)).*/\\1/p' CHANGELOG)
    expected_debian="$version"
    if [[ "$version" =~ ^([0-9]+\\.[0-9]+\\.[0-9]+)-rc([0-9]+)$ ]]; then
      expected_debian="${BASH_REMATCH[1]}~rc${BASH_REMATCH[2]}"
    fi
    [[ "$changelog_version" == "$expected_debian" ]] ||
      warn "CHANGELOG says $changelog_version but .version implies $expected_debian"
  else
    bad "Missing CHANGELOG"
  fi
  if [[ -f packaging/PKGBUILD ]]; then
    arch_version=$(sed -n 's/^pkgver=//p' packaging/PKGBUILD | head -n 1)
    [[ "$arch_version" == "$expected_arch" ]] ||
      warn "Arch pkgver=$arch_version differs from expected $expected_arch"
  fi
  if [[ -f packaging/rustyrig.spec ]]; then
    rpm_version=$(sed -n 's/^Version:[[:space:]]*//p' packaging/rustyrig.spec | head -n 1)
    [[ "$rpm_version" == "$expected_rpm" ]] ||
      warn "RPM Version=$rpm_version differs from .version=$version"
  fi
fi
if ! git diff --quiet || ! git diff --cached --quiet; then
  warn "Working tree has uncommitted tracked changes"
fi
if [[ -n "$(git ls-files --others --exclude-standard)" ]]; then
  warn "Working tree has untracked files (inspect before tagging)"
fi
note "Submodule status:"
git submodule status --recursive || bad "Unable to inspect submodules"
if git submodule status --recursive | grep -Eq '^[+-U]'; then
  warn "Submodule revision differs from recorded checkout or is uninitialized"
fi
for file in debian/files debian/.debhelper; do
  if git ls-files --error-unmatch "$file" >/dev/null 2>&1; then
    bad "Generated packaging artifact is tracked: $file"
  fi
done
if command -v dpkg >/dev/null 2>&1 && [[ -f .version ]]; then
  version=$(tr -d '[:space:]' < .version)
  if [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+-rc[0-9]+$ ]]; then
    stable=${version%-rc*}
    if dpkg --compare-versions "$version" ge "$stable"; then
      bad "Debian orders $version >= $stable; use a tilde prerelease in Debian metadata"
    fi
  fi
fi
if ((fail)); then
  note "Preflight: FAIL; review issues above."
  exit 1
fi
note "Preflight: checks completed; warnings require review. Build and runtime tests are separate."
