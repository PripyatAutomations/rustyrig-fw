#!/usr/bin/env bash
# Exercise proxy media independently of RTP build support.
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" ||
   ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h" || ! command -v openssl >/dev/null; then
   echo 'SKIP: live proxy media requires SQLite, Mongoose and openssl'
   exit 0
fi
RR_RTP_TEST_PEER= RR_PROXY_TEST=1 RR_LINK_LISTENER=shared-plain python3 rrserver/tests/links_live.py
