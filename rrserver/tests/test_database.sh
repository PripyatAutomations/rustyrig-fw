#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags glib-2.0) \
   rrserver/tests/database.c rrserver/database.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -lsqlite3 \
   $(pkg-config --libs glib-2.0) \
   -o "$work/database"
"$work/database"
