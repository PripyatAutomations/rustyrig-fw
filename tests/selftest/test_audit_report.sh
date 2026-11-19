#!/usr/bin/env bash
# Test that failed scanners cannot invent resolved vulnerabilities.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/run1" "$tmp/run2" "$tmp/run3"
printf '%s\n' ok > "$tmp/run1/flawfinder.status"
printf '%s\n' ok > "$tmp/run1/clang-tidy.status"
printf '%s\n' ok > "$tmp/run1/cppcheck.status"
printf '%s\n' 'rrserver/proto.c:42:  [4] (buffer) dangerous use' > "$tmp/run1/flawfinder.log"
printf '%s\n' 'rrclient/parse.c:10:5: warning: null dereference [clang-analyzer-core.NullDereference]' > "$tmp/run1/clang-tidy.log"
printf '%s\n' 'rrserver/proto.c:8:1: error: index outside buffer [arrayIndexOutOfBounds]' > "$tmp/run1/cppcheck.log"
python3 "$root/security/audit-report.py" "$tmp/run1" --state "$tmp/state.json" >/dev/null
printf '%s\n' ok > "$tmp/run2/flawfinder.status"
printf '%s\n' ok > "$tmp/run2/clang-tidy.status"
printf '%s\n' failed > "$tmp/run2/cppcheck.status"
: > "$tmp/run2/flawfinder.log"
: > "$tmp/run2/clang-tidy.log"
python3 "$root/security/audit-report.py" "$tmp/run2" --state "$tmp/state.json" >/dev/null
python3 - "$tmp/run2/comparison.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert len(data["resolved"]) == 2, data
assert all(x["severity"] == "severe" for x in data["resolved"]), data
assert len(data["not_checked"]) == 1, data
assert not data["new"], data
PY
printf '%s\n' ok > "$tmp/run3/flawfinder.status"
printf '%s\n' ok > "$tmp/run3/clang-tidy.status"
printf '%s\n' ok > "$tmp/run3/cppcheck.status"
: > "$tmp/run3/flawfinder.log"
: > "$tmp/run3/clang-tidy.log"
: > "$tmp/run3/cppcheck.log"
python3 "$root/security/audit-report.py" "$tmp/run3" --state "$tmp/state.json" >/dev/null
python3 - "$tmp/run3/comparison.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert len(data["resolved"]) == 1, data
assert data["resolved"][0]["scanner"] == "cppcheck", data
PY
# A native ScanView plist must enter the same resolved/new comparison.
mkdir -p "$tmp/run4/scanview" "$tmp/run5/scanview"
printf '%s\n' ok > "$tmp/run4/scanbuild.status"
: > "$tmp/run4/scanbuild.log"
python3 - "$tmp/run4/scanview/sample.plist" <<'PY'
import plistlib, sys
with open(sys.argv[1], 'wb') as out:
    plistlib.dump({'files': ['rrclient/events.c'], 'diagnostics': [
        {'category': 'Logic error', 'type': 'Dereference of null pointer',
         'description': 'Dereference of null pointer',
         'location': {'file': 0, 'line': 523, 'col': 1}}]}, out)
PY
python3 "$root/security/audit-report.py" "$tmp/run4" --state "$tmp/scanstate.json" >/dev/null
printf '%s\n' ok > "$tmp/run5/scanbuild.status"
: > "$tmp/run5/scanbuild.log"
python3 "$root/security/audit-report.py" "$tmp/run5" --state "$tmp/scanstate.json" >/dev/null
python3 - "$tmp/run5/comparison.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert len(data['resolved']) == 1, data
assert data['resolved'][0]['severity'] == 'severe', data
assert data['resolved'][0]['scanner'] == 'scanbuild', data
PY
mkdir -p "$tmp/pscan1" "$tmp/pscan2"
printf '%s\n' ok > "$tmp/pscan1/printf.status"
printf '%s\n' 'rrserver/srv.send.c:34 Warning: vsnprintf uses non-constant string for format argument 2.' > "$tmp/pscan1/printf.log"
python3 "$root/security/audit-report.py" "$tmp/pscan1" --state "$tmp/pscanstate.json" >/dev/null
printf '%s\n' ok > "$tmp/pscan2/printf.status"
: > "$tmp/pscan2/printf.log"
python3 "$root/security/audit-report.py" "$tmp/pscan2" --state "$tmp/pscanstate.json" >/dev/null
python3 - "$tmp/pscan2/comparison.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert len(data['resolved']) == 1, data
assert data['resolved'][0]['scanner'] == 'printf', data
assert data['resolved'][0]['severity'] == 'moderate', data
PY
# Distinct same-rule Flawfinder sites must not collapse into one finding.
mkdir -p "$tmp/flawdup1" "$tmp/flawdup2" "$tmp/flawdup3"
for n in 1 2 3; do
  printf '%s\n' ok > "$tmp/flawdup$n/flawfinder.status"
done
printf '%s\n' \
  'rrserver/link.tree.c:176:  [4] (buffer) strcpy:' \
  'rrserver/link.tree.c:177:  [4] (buffer) strcpy:' \
  'rrserver/link.tree.c:178:  [4] (buffer) strcpy:' > "$tmp/flawdup1/flawfinder.log"
python3 "$root/security/audit-report.py" "$tmp/flawdup1" --state "$tmp/dups.json" >/dev/null
python3 - "$tmp/flawdup1/findings.json" <<'PY'
import json, sys
rows = [f for f in json.load(open(sys.argv[1]))['findings'] if f['scanner'] == 'flawfinder']
assert len(rows) == 3, rows
PY
printf '%s\n' 'rrserver/link.tree.c:176:  [4] (buffer) strcpy:' > "$tmp/flawdup2/flawfinder.log"
python3 "$root/security/audit-report.py" "$tmp/flawdup2" --state "$tmp/dups.json" >/dev/null
python3 - "$tmp/flawdup2/comparison.json" <<'PY'
import json, sys
delta = json.load(open(sys.argv[1]))
assert len(delta['resolved']) == 2, delta
assert len(delta['open']) == 1, delta
PY
# An older identity schema is a rebaseline, not mass resolved findings.
printf '%s\n' '{"version": 1, "scanners": {"flawfinder": "ok"}, "findings": [{"id":"obsolete","scanner":"flawfinder","file":"rrserver/a.c","line":12,"severity":"severe","rule":"flawfinder.4","message":"buffer: strcpy:"}]}' > "$tmp/oldstate.json"
python3 "$root/security/audit-report.py" "$tmp/flawdup3" --state "$tmp/oldstate.json" >/dev/null
python3 - "$tmp/flawdup3/comparison.json" <<'PY'
import json, sys
delta = json.load(open(sys.argv[1]))
assert not delta['resolved'], delta
assert not delta['new'], delta
PY
echo 'PASS: audit baseline and severe resolved regression checks'
