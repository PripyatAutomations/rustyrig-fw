#!/usr/bin/env bash
# Run component-owned test suites. Usage: run-tests.sh [suite ...]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
  exec "$ROOT/tests/with-xvfb.sh" bash "$ROOT/tests/run-tests.sh" "$@"
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

run_dir() {
  local label="$1" dir="$2" total=0 t
  echo "=== suite: $label ($dir) ==="
  if [ -f "$dir/GNUmakefile" ] || [ -f "$dir/Makefile" ]; then
    make -C "$dir" check || total=$((total+1))
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
    if timeout "${RR_TEST_TIMEOUT:-180}" bash "$t"; then :; else total=$((total+1)); fi
  done
  return "$total"
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
    selftest)    run_dir "$suite" tests/selftest ;;
    packaging)   run_dir "$suite" packaging/tests ;;
    librustyaxe) run_dir "$suite" librustyaxe/tests ;;
    www)         node www/tests/audio_routes.js && node www/tests/chat_rooms.js && node www/tests/auth.js && node www/tests/wire.js && node www/tests/media_health.js && node www/tests/audio_backlog.js && node www/tests/binframe_validation.js && node www/tests/chat_security.js && node www/tests/web_completion.js && node www/tests/media_rooms.js && node www/tests/ptt_pending.js && node www/tests/reconnect.js && node www/tests/gps_nmea.js && node www/tests/resource_discovery.js && node www/tests/resource_navigation.js && node www/tests/webcam.js ;;
    *) echo "Unknown test suite: $suite" >&2; return 1 ;;
  esac
}

if [ "$#" -eq 0 ]; then
  SUITES="selftest packaging librustyaxe librrprotocol rrserver rrclient rrbridge fwdsp libfwdspmgr www"
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
