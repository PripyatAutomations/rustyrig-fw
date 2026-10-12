#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: direct media policy requires native WebSocket control'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/media_direct.c rrclient/media.direct.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -o "$work/media-direct"
"$work/media-direct"
