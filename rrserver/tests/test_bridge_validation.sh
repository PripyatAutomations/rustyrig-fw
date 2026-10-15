#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/bridge_validation.c \
   rrserver/rig.properties.c -Wl,--gc-sections $(pkg-config --cflags --libs glib-2.0) \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -lfwdspmgr ${LDFLAGS:-} -o "$work/bridge-validation"
"$work/bridge-validation"
