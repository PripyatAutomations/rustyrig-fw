#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -I. \
   rrserver/tests/si5351.c rrserver/si5351.c -lm ${LDFLAGS:-} -o "$work/si5351"
"$work/si5351"
${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrserver/tests/si5351_registry.c rrserver/hardware.c rrserver/si5351.c \
   rrserver/hardware.si5351.c rrserver/cfg.si5351.c rrserver/cfg.rig.c \
   $(pkg-config --cflags --libs glib-2.0) \
   -L. -Wl,-rpath,"$PWD" -lrustyaxe ${LDFLAGS:-} -o "$work/registry"
: > "$work/not-i2c"
"$work/registry" "$work/not-i2c" "$work/config"
