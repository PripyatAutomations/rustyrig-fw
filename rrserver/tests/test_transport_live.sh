#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" ||
   ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: transport live test requires SQLite and Mongoose'
   exit 0
fi
python3 rrserver/tests/transport_live.py
