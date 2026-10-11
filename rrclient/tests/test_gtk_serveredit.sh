#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
pkg-config --exists gtk+-3.0 || { echo 'SKIP: GTK headers unavailable'; exit 0; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_GTK=1 -Wno-deprecated-declarations -I. -Iinc -Ibuild/${PROFILE:-radio} \
  -ffunction-sections -fdata-sections $(pkg-config --cflags gtk+-3.0) \
  rrclient/tests/gtk_serveredit.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
  -lrustyaxe -lrrprotocol $(pkg-config --libs gtk+-3.0) -o "$work/serveredit"
if command -v xvfb-run >/dev/null && [[ -z ${DISPLAY:-} ]]; then
  xvfb-run -a "$work/serveredit"
else
  "$work/serveredit"
fi
