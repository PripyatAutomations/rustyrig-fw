#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: relay users requires server links'; exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags glib-2.0) \
   -ffunction-sections -fdata-sections rrserver/tests/relay_users.c -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe $(pkg-config --libs glib-2.0) \
   -o "$work/relay-users"
"$work/relay-users"
