#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/backend_workers.c \
   rrserver/backend.worker.c rrserver/backend.instance.c rrserver/rig.registry.c rrserver/rig.properties.c \
   $(pkg-config --cflags --libs glib-2.0) -pthread -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/backend_workers"
"$work/backend_workers"
