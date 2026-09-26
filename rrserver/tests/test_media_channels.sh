#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/media_channels.c \
   rrserver/media.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lfwdspmgr -lrustyaxe ${LDFLAGS:-} -o "$work/media_channels"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/media_channels"
