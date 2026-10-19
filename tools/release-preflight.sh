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
  if [[ "$version" =~ -rc[0-9]+$ ]]; then
    warn "RC suffix uses a hyphen: ensure Debian package version is ~rcN so stable upgrades correctly"
  fi
  [[ -f CHANGELOG ]] && head -n 1 CHANGELOG || bad "Missing CHANGELOG"
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
