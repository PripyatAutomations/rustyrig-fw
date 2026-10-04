#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   rrclient/tests/ptt_ack.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol -o "$work/ptt_ack"
"$work/ptt_ack"
if pkg-config --exists gtk+-3.0; then
   ${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
      $(pkg-config --cflags gtk+-3.0) rrclient/tests/ptt_pending.c \
      -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol \
      $(pkg-config --libs gtk+-3.0) -o "$work/ptt_pending"
   "$work/ptt_pending"
else
   echo 'SKIP: GTK PTT presentation test requires GTK development headers'
fi
