#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/ptt_targets.c rrserver/backend.c \
   rrserver/rig.registry.c rrserver/backend.instance.c rrserver/rig.properties.c \
   $(pkg-config --cflags --libs glib-2.0) -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -o "$work/ptt_targets"
"$work/ptt_targets"
