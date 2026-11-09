#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/media_direct.c rrclient/media.direct.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe $(pkg-config --cflags --libs glib-2.0) -o "$work/media-direct"
"$work/media-direct"
