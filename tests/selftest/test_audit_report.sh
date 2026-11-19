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
python3 "$root/security/audit-report.py" "$tmp/run5" --state "$tmp/scanstate.json" >/dev/null
python3 - "$tmp/run5/comparison.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert len(data['resolved']) == 1, data
assert data['resolved'][0]['severity'] == 'severe', data
assert data['resolved'][0]['scanner'] == 'scanbuild', data
PY
echo 'PASS: audit baseline and severe resolved regression checks'
