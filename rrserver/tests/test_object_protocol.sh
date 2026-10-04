#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists hamlib sqlite3 || ! command -v rigctld >/dev/null; then
   echo 'SKIP: object protocol live test requires Hamlib/SQLite/rigctld'
   exit 0
fi
if ! grep -q '^#define USE_HAMLIB' "build/${PROFILE:-radio}/build_config.h" ||
   ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: object protocol live test requires a Hamlib/SQLite-enabled profile'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -DRR_TEST_OBJECT_PROTOCOL -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags glib-2.0 hamlib sqlite3) \
   rrserver/tests/multirig_live.c rrserver/backend.c \
   rrserver/backend.instance.c rrserver/backend.register.c \
   rrserver/backend.internal.c rrserver/backend.hamlib.c \
   rrserver/rig.properties.c rrserver/rig.compat.c rrserver/rig.registry.c \
   rrserver/rig.config.c rrserver/rig.rooms.c rrserver/database.c rrserver/defconfig.c \
   rrserver/objects.c rrclient/objects.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   $(pkg-config --libs glib-2.0 hamlib sqlite3) ${LDFLAGS:-} \
   -o "$work/multirig-live"
python3 rrserver/tests/multirig_live.py "$work/multirig-live" "$work"
