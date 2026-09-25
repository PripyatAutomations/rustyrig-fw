#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrserver/tests/database.c rrserver/database.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -lsqlite3 \
   -o "$work/database"
"$work/database"
