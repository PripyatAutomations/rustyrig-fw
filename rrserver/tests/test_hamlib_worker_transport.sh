#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
profile="${PROFILE:-radio}"
# Base protocol and fake-child tests do not need Hamlib.
make --no-print-directory PROFILE="$profile" test-hamlib-ipc test-hamlib-transport
if ! grep -q '^#define USE_HAMLIB' "build/$profile/build_config.h" 2>/dev/null; then
   echo 'SKIP: real Hamlib dummy rig requires a Hamlib-enabled build profile'
elif ! command -v rigctld >/dev/null; then
   echo 'SKIP: real Hamlib dummy rig requires rigctld'
else
   make --no-print-directory PROFILE="$profile" test-hamlib-dummy
fi
echo 'PASS: Hamlib IPC framing and worker transport suite'
