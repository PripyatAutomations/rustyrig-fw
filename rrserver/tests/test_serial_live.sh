#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-rrserver}/build_config.h" || ! command -v openssl >/dev/null; then
   echo 'SKIP: serial live test requires SQLite and OpenSSL'
   exit 0
fi
python3 rrserver/tests/serial_live.py
