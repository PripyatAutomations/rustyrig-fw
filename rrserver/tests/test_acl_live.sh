#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-rrserver}/build_config.h"; then
   echo 'SKIP: listener ACL integration requires SQLite'
   exit 0
fi
if ! command -v openssl >/dev/null; then
   echo 'SKIP: native TLS listener ACL integration requires openssl'
   exit 0
fi
python3 rrserver/tests/acl_live.py
