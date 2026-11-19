#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

# Match the selected server build, whether using Mongoose or native transport.
profile=${PROFILE:-rrserver}
config="build/$profile/build_config.h"
if [[ ! -f $config ]]; then
   make PROFILE="$profile" buildconf
fi
if [[ ! -f $config ]]; then
   echo "FAIL: missing generated build configuration: $config" >&2
   exit 1
fi
if ! grep -Eq '^[[:space:]]*#define[[:space:]]+USE_SQLITE([[:space:]]|$)' "$config"; then
   echo 'SKIP: file shares require SQLite'
   exit 0
fi
python3 rrserver/tests/files_live.py
