#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then
   echo 'SKIP: mode widget test requires GTK3'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_GTK -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   rrclient/tests/mode_box.c rrclient/gtk/gtk.core.c -Wl,--gc-sections \
   $(pkg-config --cflags --libs gtk+-3.0) -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol -o "$work/mode_box"
status=0
"$work/mode_box" || status=$?
[ "$status" = 0 ] || [ "$status" = 77 ]
