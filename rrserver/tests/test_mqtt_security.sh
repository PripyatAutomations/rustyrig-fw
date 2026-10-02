#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrserver/tests/mqtt_security.c \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/mqtt_security"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   "$work/mqtt_security"
