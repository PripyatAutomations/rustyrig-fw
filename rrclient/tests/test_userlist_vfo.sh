#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then
   echo 'SKIP: userlist VFO selection test requires GTK development headers'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_GTK=1 -Wno-deprecated-declarations -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   $(pkg-config --cflags gtk+-3.0) rrclient/tests/userlist_vfo.c \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol \
   $(pkg-config --libs gtk+-3.0) -o "$work/userlist_vfo"
"$work/userlist_vfo"
