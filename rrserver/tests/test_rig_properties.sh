#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} ${CFLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrserver/tests/rig_properties.c rrserver/rig.properties.c \
   rrserver/rig.compat.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} \
   -o "$work/rig_properties"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   "$work/rig_properties"
