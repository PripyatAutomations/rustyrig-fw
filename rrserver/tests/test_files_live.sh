#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" || ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: file shares require SQLite and WebSockets'; exit 0
fi
python3 rrserver/tests/files_live.py
