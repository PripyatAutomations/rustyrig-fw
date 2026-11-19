#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: native file shares require SQLite'; exit 0
fi
python3 rrserver/tests/files_live.py
