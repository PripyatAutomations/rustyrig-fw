#!/usr/bin/env bash
# Run component-owned test suites. Usage: run-tests.sh [suite ...]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
  exec "$ROOT/tests/with-xvfb.sh" bash "$ROOT/tests/run-tests.sh" "$@"
fi

# Many component tests compile small C harnesses directly and rely on GLib
# headers without going through the project makefiles. Keep their compiler
# search path consistent with the normal build's pkg-config flags.
if pkg-config --exists glib-2.0; then
  for include_flag in $(pkg-config --cflags-only-I glib-2.0); do
    if [[ $include_flag == -I* ]]; then
      C_INCLUDE_PATH="${C_INCLUDE_PATH:+$C_INCLUDE_PATH:}${include_flag#-I}"
    fi
  done
  export C_INCLUDE_PATH
fi

SKIP_CODECS=0
if [[ ${1:-} == --skip-codecs ]]; then
  SKIP_CODECS=1
  shift
fi
if [[ ${1:-} == --help ]]; then
  echo 'Usage: tests/run-tests.sh [--skip-codecs] [suite ...]'
  echo 'All suites run inside Xvfb. --skip-codecs omits codec-specific checks and DSP codec integrations.'
  exit 0
fi

# Wait asynchronously so a signal to the runner itself can interrupt wait and
# cancel the active supervisor, rather than waiting for the test to finish.
ACTIVE_TEST=
stop_tests() {
  local code="$1"
  trap '' INT TERM HUP
  if [[ -n "$ACTIVE_TEST" ]]; then
    kill -TERM "$ACTIVE_TEST" 2>/dev/null || true
    wait "$ACTIVE_TEST" 2>/dev/null || true
  fi
  exit "$code"
}
trap 'stop_tests 130' INT
trap 'stop_tests 143' TERM
trap 'stop_tests 129' HUP
run_test() {
  local result
  python3 "$ROOT/tests/run-command.py" --timeout "${RR_TEST_TIMEOUT:-180}" -- "$@" &
  ACTIVE_TEST=$!
  if wait "$ACTIVE_TEST"; then result=0; else result=$?; fi
  ACTIVE_TEST=
  case "$result" in
    129|130|143) exit "$result" ;;
  esac
  if [[ "$result" != 0 ]]; then
    echo "FAIL ($result): $*" >&2
  fi
  return "$result"
}

run_dir() {
  local label="$1" dir="$2" total=0 t
  echo "=== suite: $label ($dir) ==="
  if [ -f "$dir/GNUmakefile" ] || [ -f "$dir/Makefile" ]; then
    run_test make -C "$dir" check || total=$((total+1))
  fi
  for t in "$dir"/test_*.sh; do
    [ -e "$t" ] || continue
    if [[ $SKIP_CODECS == 1 ]]; then
      case "$t" in
        */test_codec*.sh|*/test_gtk_codec_picker.sh|fwdsp/tests/test_switching.sh|fwdsp/tests/test_video.sh|fwdsp/tests/test_startup_stdout.sh|fwdsp/tests/test_recording_names.sh)
          echo "SKIP (--skip-codecs): $t"; continue ;;
      esac
    fi
    echo "--- test: $t ---"
    if run_test bash "$t"; then :; else total=$((total+1)); fi
  done
  [[ "$total" == 0 ]]
}

run_suite() {
  local suite="$1"
  case "$suite" in
    libfwdspmgr) run_dir "$suite" libfwdspmgr/tests ;;
    rrbridge)    run_dir "$suite" rrbridge/tests ;;
    fwdsp)       run_dir "$suite" fwdsp/tests ;;
    librrprotocol) run_dir "$suite" librrprotocol/tests ;;
    rrclient)    run_dir "$suite" rrclient/tests ;;
    rrserver)    run_dir "$suite" rrserver/tests ;;
    rrpipelines) run_dir "$suite" rrpipelines/tests ;;
    selftest)    run_dir "$suite" tests/selftest ;;
    packaging)   run_dir "$suite" packaging/tests ;;
    librustyirc) run_dir "$suite" librustyirc/tests ;;
    librustyaxe) run_dir "$suite" librustyaxe/tests ;;
    www)
      local failed=0 test found=0
      for test in www/tests/*.js; do
        [[ -f "$test" ]] || continue
        found=1
        run_test node "$test" || failed=1
      done
      if [[ $found == 0 ]]; then
        echo "SKIP: no browser tests installed (www was removed)"
      fi
      return "$failed" ;;
    *) echo "Unknown test suite: $suite" >&2; return 1 ;;
  esac
}

if [ "$#" -eq 0 ]; then
  SUITES="selftest packaging librustyaxe librustyirc librrprotocol rrserver rrclient rrbridge fwdsp libfwdspmgr rrpipelines www"
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
