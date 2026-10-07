#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists hamlib || ! grep -q "^#define USE_HAMLIB" "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: Hamlib TX test requires Hamlib headers'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/hamlib_tx.c \
   rrserver/rig.registry.c rrserver/backend.instance.c rrserver/rig.properties.c \
   $(pkg-config --cflags --libs glib-2.0 hamlib) -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -o "$work/hamlib_tx"
"$work/hamlib_tx"
