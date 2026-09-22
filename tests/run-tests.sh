#!/usr/bin/env bash
# Run component-owned test suites. Usage: run-tests.sh [suite ...]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

run_dir() {
  local label="$1" dir="$2" total=0 t
  echo "=== suite: $label ($dir) ==="
  if [ -f "$dir/GNUmakefile" ] || [ -f "$dir/Makefile" ]; then
    make -C "$dir" check || total=$((total+1))
  fi
  for t in "$dir"/test_*.sh; do
    [ -e "$t" ] || continue
    if bash "$t"; then :; else total=$((total+1)); fi
  done
  return "$total"
}

run_suite() {
  local suite="$1"
  case "$suite" in
    fwdsp)       run_dir "$suite" fwdsp/tests ;;
    librrprotocol) run_dir "$suite" librrprotocol/tests ;;
    rrclient)    run_dir "$suite" rrclient/tests ;;
    rrserver)    run_dir "$suite" rrserver/tests ;;
    selftest)    run_dir "$suite" tests/selftest ;;
    librustyaxe) make -C librustyaxe/tests check ;;
    www)         node www/tests/web_completion.js ;;
    *) echo "Unknown test suite: $suite" >&2; return 1 ;;
  esac
}

if [ "$#" -eq 0 ]; then
  SUITES="fwdsp librrprotocol rrclient rrserver selftest librustyaxe www"
else
  SUITES="$*"
fi

TOTAL_FAIL=0
for suite in $SUITES; do
  if run_suite "$suite"; then :; else TOTAL_FAIL=$((TOTAL_FAIL+1)); fi
done

if [ "$TOTAL_FAIL" -eq 0 ]; then
  echo "ALL SUITES PASSED"
else
  echo "$TOTAL_FAIL SUITE(S) FAILED"
  exit 1
fi
