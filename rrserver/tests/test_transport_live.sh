#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" ||
   ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: transport live test requires SQLite and Mongoose'
   exit 0
fi
for failure in audit quota usage ptt-start readonly; do
   RR_TEST_ACCOUNTING_FAILURE="$failure" python3 rrserver/tests/transport_live.py
done
