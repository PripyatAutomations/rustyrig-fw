#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/media_channels.c \
   rrserver/media.c rrserver/rig.registry.c rrserver/backend.instance.c \
   rrserver/rig.properties.c rrserver/cfg.rig.c rrserver/rig.rooms.c \
   rrserver/database.c $(pkg-config --cflags --libs sqlite3 glib-2.0) -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lfwdspmgr -lrustyaxe ${LDFLAGS:-} -o "$work/media_channels"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/media_channels"
