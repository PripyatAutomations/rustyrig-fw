#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags glib-2.0) \
   rrserver/tests/multirig.c rrserver/backend.c \
   rrserver/backend.instance.c rrserver/rig.properties.c \
   rrserver/rig.compat.c rrserver/rig.registry.c rrserver/rig.config.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   $(pkg-config --libs glib-2.0) ${LDFLAGS:-} \
   -o "$work/multirig"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   "$work/multirig"
