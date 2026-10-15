#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections $(pkg-config --cflags glib-2.0) \
   rrserver/tests/bridge_access.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol $(pkg-config --libs glib-2.0) -o "$work/bridge-access"
"$work/bridge-access"

${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections $(pkg-config --cflags glib-2.0) \
   rrserver/tests/bridge_privileges.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol $(pkg-config --libs glib-2.0) -o "$work/bridge-privileges"
"$work/bridge-privileges"
