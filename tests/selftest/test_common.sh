#!/usr/bin/env bash
# Sanity-check the reusable assertion helpers in common.sh.
. "$(dirname "$0")/../common.sh"

assert_eq foo foo "eq pass"
assert_eq foo foo "eq pass 2"
assert_contains "hello world" "world" "contains pass"
assert_ok true "ok pass"

# Negative case: a failing assertion must not abort, only bump FAIL.
assert_eq a b "expected failure" >/dev/null 2>&1
assert_eq "$PASS" 4 "pass counter"
assert_eq "$FAIL" 1 "fail counter"
FAIL=0  # reset so the suite exits clean

test_finished
