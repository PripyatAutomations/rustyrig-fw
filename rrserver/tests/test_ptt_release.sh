#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: PTT release fixture requires SQLite'
   exit 0
fi
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/ptt_release.c rrserver/ptt.c \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -lsqlite3 \
   -o "$work/ptt_release"
"$work/ptt_release"
