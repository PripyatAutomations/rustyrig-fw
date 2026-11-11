#!/usr/bin/env bash
# Bump this source project's upstream version and release packaging metadata.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

usage() {
   cat <<'HELP'
Usage: tools/bump-version.sh [--patch|--minor|--major] [--dry-run]
       tools/bump-version.sh --version X.Y.Z[-rcN] [--dry-run]

Without a mode, increment the patch version (1.2.3 -> 1.2.4).
  --minor      Increment minor, reset patch (1.2.3 -> 1.3.0)
  --major      Increment major, reset minor and patch (1.2.3 -> 2.0.0)
  --patch      Explicit default (increment patch)
  --version V  Set an explicit version (also accepts vX.Y.Z)
  --dry-run    Show the proposed version without changing files
  --help       Show this message

Updates .version, Debian changelog (new version starts at -1), Arch
PKGBUILD (pkgrel=1), RPM spec (Release: 1), and project CHANGELOG if present.
Existing changelog history is preserved. This script does not commit or tag.
HELP
}

mode=patch
requested=""
dry_run=false
seen_mode=false
while (($#)); do
   case "$1" in
      --patch|--minor|--major)
         if $seen_mode || [[ -n $requested ]]; then
            echo "Select only one of --patch, --minor, --major or --version" >&2
            exit 2
         fi
         mode=${1#--}
         seen_mode=true
         shift ;;
      --version)
         if $seen_mode || [[ -n $requested ]] || (($# < 2)); then
            echo "--version requires one version and cannot be combined with a bump mode" >&2
            exit 2
         fi
         requested=${2#v}
         shift 2 ;;
      --dry-run) dry_run=true; shift ;;
      -h|--help) usage; exit 0 ;;
      *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
   esac
done

[[ -f .version ]] || { echo "Missing .version in $ROOT" >&2; exit 1; }
old=$(tr -d '[:space:]' < .version)
if [[ ! $old =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-rc[0-9]+)?$ ]]; then
   echo "Expected semantic X.Y.Z in .version, got: $old" >&2
   exit 1
fi
major=${BASH_REMATCH[1]}
minor=${BASH_REMATCH[2]}
patch=${BASH_REMATCH[3]}
if [[ -n $requested ]]; then
   [[ $requested =~ ^[0-9]+\.[0-9]+\.[0-9]+(-rc[0-9]+)?$ ]] || {
      echo "Invalid version: $requested" >&2; exit 2;
   }
   next=$requested
else
   case $mode in
      major) next="$((10#$major + 1)).0.0" ;;
      minor) next="$((10#$major)).$((10#$minor + 1)).0" ;;
      patch) next="$((10#$major)).$((10#$minor)).$((10#$patch + 1))" ;;
   esac
fi
if [[ $next == "$old" ]]; then
   echo "Version is already $old; nothing to do" >&2
   exit 1
fi

# Python handles history insertion and metadata updates without modifying
# unrelated version numbers (e.g. ABI compatibility or dependency constraints).
python3 - "$next" "$old" "$dry_run" <<'PY'
from datetime import datetime, timezone
from email.utils import format_datetime
from pathlib import Path
import re
import sys

next_version, previous, dry_run = sys.argv[1:]
root = Path.cwd()
release = re.fullmatch(r'(\d+\.\d+\.\d+)-rc(\d+)', next_version)
debian_upstream = f'{release[1]}~rc{release[2]}' if release else next_version
arch_version = f'{release[1]}_rc{release[2]}' if release else next_version
rpm_version = release[1] if release else next_version
rpm_release = f'0.rc{release[2]}%{{?dist}}' if release else '1%{?dist}'

changelog = root / 'debian/changelog'
if not changelog.is_file():
    raise SystemExit('Missing debian/changelog: refusing an incomplete version bump')
history = changelog.read_text()
header = re.match(r'^([^\s(]+) \(([^)]+)\)\s+[^\n]+\n', history)
if not header:
    raise SystemExit('Invalid Debian changelog header')
package = header[1]
new_debian = f'{debian_upstream}-1'

# Keep the actual packager identity from the existing Debian history.
match = re.search(r'^ -- (.+?)  [^\n]+$', history, flags=re.MULTILINE)
identity = match[1] if match else 'Pripyat Automations <dangerousdevices@istabpeople.com>'
stamp = format_datetime(datetime.now(timezone.utc).astimezone())
entry = (f'{package} ({new_debian}) unstable; urgency=medium\n\n'
         f'  * Release upstream version {next_version}.\n\n'
         f' -- {identity}  {stamp}\n\n')
updates = {root / '.version': next_version + '\n',
           changelog: entry + history}

# RustyRig's independent, human-readable source history is *not* the Debian
# changelog. Prepend a source release note without touching prior entries.
source_history = root / 'CHANGELOG'
if source_history.is_file():
    existing = source_history.read_text()
    if not re.match(r'^' + re.escape(package) + r' \([^)]+\)', existing):
        raise SystemExit('Unexpected CHANGELOG header; refusing to replace it')
    source_entry = (f'{package} ({debian_upstream}) testing; urgency=medium\n\n'
                    f'  * Prepare {package} {next_version} release.\n\n'
                    f' -- {identity}  {stamp}\n\n')
    updates[source_history] = source_entry + existing

arch_paths = [root / 'packaging/PKGBUILD', root / 'PKGBUILD']
for path in arch_paths:
    if not path.is_file():
        continue
    content = path.read_text()
    content, n = re.subn(r'^pkgver=.*$', f'pkgver={arch_version}', content, count=1, flags=re.M)
    if n != 1:
        raise SystemExit(f'Missing pkgver in {path}')
    content, n = re.subn(r'^pkgrel=.*$', 'pkgrel=1', content, count=1, flags=re.M)
    if n != 1:
        raise SystemExit(f'Missing pkgrel in {path}')
    updates[path] = content

for path in sorted((root / 'packaging').glob('*.spec')):
    content = path.read_text()
    content, n = re.subn(r'^Version:\s*.*$', f'Version: {rpm_version}', content, count=1, flags=re.M)
    if n != 1:
        raise SystemExit(f'Missing Version: in {path}')
    content, n = re.subn(r'^Release:\s*.*$', f'Release: {rpm_release}', content, count=1, flags=re.M)
    if n != 1:
        raise SystemExit(f'Missing Release: in {path}')
    updates[path] = content

print(f'{package}: {previous} -> {next_version} (Debian {new_debian}, Arch {arch_version}-1, RPM {rpm_version}-{rpm_release})')
for path in updates:
    print(('Would update: ' if dry_run == 'true' else 'Updating: ') + str(path.relative_to(root)))
if dry_run == 'true':
    sys.exit(0)
for path, content in updates.items():
    path.write_text(content)
PY
