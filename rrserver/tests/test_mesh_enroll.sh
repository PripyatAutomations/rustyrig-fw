#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -Werror -DUSE_SQLITE \
   -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags glib-2.0 sqlite3) \
   rrserver/tests/mesh_enroll.c rrserver/identity.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   $(pkg-config --libs glib-2.0 sqlite3) ${LDFLAGS:-} -o "$work/mesh_enroll"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/mesh_enroll"
