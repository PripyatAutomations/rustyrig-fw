#!/usr/bin/env bash
# Run every test suite found under tests/. Usage: run-tests.sh [suite...]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SUITES="$*"
[ -z "$SUITES" ] && SUITES="$(ls tests | grep -v -e common.sh -e run-tests.sh -e README.md)"

TOTAL_FAIL=0
for s in $SUITES; do
  dir="tests/$s"
  echo "=== suite: $s ==="
  if [ -f "$dir/GNUmakefile" ] || [ -f "$dir/Makefile" ]; then
    make -C "$dir" check || { TOTAL_FAIL=$((TOTAL_FAIL+1)); continue; }
  fi
  for t in "$dir"/test_*.sh; do
    [ -e "$t" ] || continue
    if bash "$t"; then :; else TOTAL_FAIL=$((TOTAL_FAIL+1)); fi
  done
done

if [ "$TOTAL_FAIL" -eq 0 ]; then echo "ALL SUITES PASSED"; else echo "$TOTAL_FAIL SUITE(S) FAILED"; exit 1; fi
