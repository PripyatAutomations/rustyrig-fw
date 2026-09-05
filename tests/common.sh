#!/usr/bin/env bash
# Reusable assertion helpers for shell-based tests.
# Source this from a test script:  . "$(dirname "$0")/../common.sh"

PASS=0
FAIL=0

assert_ok() { # cmd is ok (exit 0)
  if "$@" >/dev/null 2>&1; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); echo "FAIL: $*"; fi
}

assert_eq() { # got expected [label]
  if [ "$1" = "$2" ]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); echo "FAIL($3): '$1' != '$2'"; fi
}

assert_contains() { # haystack needle [label]
  case "$1" in *"$2"*) PASS=$((PASS+1));; *) FAIL=$((FAIL+1)); echo "FAIL($3): '$1' missing '$2'";; esac
}

test_finished() {
  echo "-- $(basename "$0"): $PASS passed, $FAIL failed"
  [ "$FAIL" -eq 0 ]
}
