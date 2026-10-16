#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Exercise the disabled adapter everywhere, and the real v2 adapter when available.
for mode in disabled enabled; do
   flags=()
   if [[ $mode == enabled ]]; then
      if ! pkg-config --atleast-version=2.0 libgpiod; then
         echo 'SKIP: GPIO-enabled hardware check needs libgpiod >= 2.0'
         continue
      fi
      flags=(-DUSE_GPIO $(pkg-config --cflags --libs libgpiod))
   fi
   ${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
      rrserver/tests/hardware.c rrserver/hardware.c rrserver/gpio.c \
      rrserver/cfg.gpio.c rrserver/cfg.rig.c \
      $(pkg-config --cflags --libs glib-2.0) "${flags[@]}" \
      -L. -Wl,-rpath,"$PWD" -lrustyaxe ${LDFLAGS:-} -o "$work/hardware-$mode"
   "$work/hardware-$mode"
done
