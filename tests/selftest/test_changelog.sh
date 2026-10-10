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

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

check_changelog() {
   python3 - "$1" <<'PY' || return 1
import email.utils
import pathlib
import re
import sys

entries = [entry for entry in re.split(r'(?=^rustyrig-fw \()', pathlib.Path(sys.argv[1]).read_text(), flags=re.MULTILINE) if entry]
if not entries:
    raise SystemExit('FAIL: empty changelog')
for entry in entries:
    trailers = re.findall(r'^ -- .+ <[^<>]+>  (.+)$', entry, flags=re.MULTILINE)
    if len(trailers) != 1 or not entry.rstrip().splitlines()[-1].startswith(' -- '):
        raise SystemExit(f'FAIL: missing/duplicate/nonfinal trailer: {entry.splitlines()[0]}')
    if not re.fullmatch(r'[A-Z][a-z]{2}, \d{2} [A-Z][a-z]{2} \d{4} \d{2}:\d{2}:\d{2} [+-]\d{4}', trailers[0]):
        raise SystemExit(f'FAIL: malformed changelog timestamp: {trailers[0]}')
    if email.utils.parsedate_to_datetime(trailers[0]).tzinfo is None:
        raise SystemExit('FAIL: changelog timestamp lacks timezone')
PY
   if command -v dpkg-parsechangelog >/dev/null 2>&1; then
      dpkg-parsechangelog --all -l "$1" > /dev/null 2> "$work/parser.warnings" || return 1
      if [[ -s "$work/parser.warnings" ]]; then
         cat "$work/parser.warnings" >&2
         return 1
      fi
   fi
}

check_changelog debian/changelog
mkdir -p "$work/tools" "$work/debian"
cp tools/bump-version.sh "$work/tools/"
cp CHANGELOG "$work/"
printf '%s\n' '20261003.01' > "$work/.version"
ln -s ../CHANGELOG "$work/debian/changelog"
# Use a fixed RFC 2822 timestamp to verify the bump actually refreshes it.
mkdir "$work/bin"
cat > "$work/bin/date" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' 'Sat, 10 Oct 2026 15:16:17 -0400'
EOF
chmod +x "$work/bin/date"
export PATH="$work/bin:$PATH"
bash "$work/tools/bump-version.sh" --version 1.2.3 --dry-run >/dev/null
cmp CHANGELOG "$work/CHANGELOG"
[[ "$(cat "$work/.version")" == '20261003.01' ]]
bash "$work/tools/bump-version.sh" --version 1.2.3 >/dev/null

if [[ ! -L "$work/debian/changelog" ]] ||
   ! grep -q '^rustyrig-fw (1\.2\.3) ' "$work/CHANGELOG"; then
   echo "FAIL: version bump did not preserve the changelog symlink" >&2
   exit 1
fi

check_changelog "$work/debian/changelog"
grep -q '^ -- .*  Sat, 10 Oct 2026 15:16:17 -0400$' "$work/CHANGELOG"
# Check historical entries as well, and reject parser warnings even on exit 0.
python3 - "$work/CHANGELOG" "$work/bad-history" "$work/bad-header" <<'PY'
import pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text()
trailers = list(re.finditer(r'^ -- .+\n', text, flags=re.MULTILINE))
second = trailers[1]
pathlib.Path(sys.argv[2]).write_text(text[:second.start()] + text[second.end():])
pathlib.Path(sys.argv[3]).write_text(text.replace('urgency=medium', 'unexpected-field=medium', 1))
PY
if check_changelog "$work/bad-history" > /dev/null 2>&1; then
   echo "FAIL: missing historical trailer accepted" >&2
   exit 1
fi
if command -v dpkg-parsechangelog >/dev/null 2>&1 &&
   check_changelog "$work/bad-header" > /dev/null 2>&1; then
   echo "FAIL: changelog parser warnings accepted" >&2
   exit 1
fi
python3 - "$work/CHANGELOG" <<'PY'
import pathlib, re, sys
original = [entry for entry in re.split(r'(?=^rustyrig-fw \()', pathlib.Path('CHANGELOG').read_text(), flags=re.MULTILINE) if entry]
updated = [entry for entry in re.split(r'(?=^rustyrig-fw \()', pathlib.Path(sys.argv[1]).read_text(), flags=re.MULTILINE) if entry]
assert original[1:] == updated[1:], 'version bump changed historical entries'
path = pathlib.Path(sys.argv[1])
path.write_text(re.sub(r'^ -- .+\n', '', path.read_text(), count=1, flags=re.MULTILINE))
PY
if check_changelog "$work/CHANGELOG" > /dev/null 2>&1; then
   echo "FAIL: missing trailer accepted" >&2
   exit 1
fi
bash "$work/tools/bump-version.sh" --version 1.2.4 >/dev/null
check_changelog "$work/CHANGELOG"
# A standalone first entry must also acquire a trailer without an older identity.
printf 'rustyrig-fw (1.2.4) testing; urgency=medium\n\n  * Test.\n' > "$work/CHANGELOG"
bash "$work/tools/bump-version.sh" --version 1.2.5 >/dev/null
check_changelog "$work/CHANGELOG"

echo "PASS: all Debian changelog trailers/timestamps are valid; bumps refresh/restore trailers and preserve history, dry-run and symlink"
