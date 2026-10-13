#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0 || ! command -v xvfb-run >/dev/null; then
   echo 'SKIP: GTK admin test requires GTK3 and xvfb-run'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags gtk+-3.0) \
   -ffunction-sections -fdata-sections rrclient/tests/gtk_admin.c -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol $(pkg-config --libs gtk+-3.0) -o "$work/gtk_admin"
xvfb-run -a "$work/gtk_admin"
