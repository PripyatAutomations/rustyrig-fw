#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrclient/tests/media_direct.c rrclient/media.direct.c rrclient/irc.compat.c \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -lrustyirc $(pkg-config --cflags --libs glib-2.0) -o "$work/media-direct"
"$work/media-direct"
