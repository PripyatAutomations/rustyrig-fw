#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/room_confirmation.c rrserver/database.c \
   $(pkg-config --cflags --libs glib-2.0) -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -lsqlite3 -o "$work/room_confirmation"
"$work/room_confirmation"
