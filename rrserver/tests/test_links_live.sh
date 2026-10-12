#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" ||
   ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h" || ! command -v openssl >/dev/null; then
   echo 'SKIP: live server links require SQLite, Mongoose and openssl'
   exit 0
fi
python3 rrserver/tests/links_live.py
