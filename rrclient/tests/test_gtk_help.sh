#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0 || ! command -v xvfb-run >/dev/null; then
   echo 'SKIP: GTK help requires GTK3 and Xvfb'; exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_GTK=1 -Wno-deprecated-declarations -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   $(pkg-config --cflags gtk+-3.0) rrclient/tests/gtk_help.c rrclient/cmd.help.c \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   $(pkg-config --libs gtk+-3.0) -o "$work/help"
G_DEBUG=fatal-warnings xvfb-run -a "$work/help"
