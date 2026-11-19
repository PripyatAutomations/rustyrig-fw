#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags glib-2.0) \
   rrserver/tests/ptt_disconnect_oom.c rrserver/srv.ptt.disconnect.c \
   -lrrprotocol -lrustyaxe $(pkg-config --libs glib-2.0) \
   -o "$work/ptt-disconnect-oom"
"$work/ptt-disconnect-oom"
echo 'PASS: OOM during disconnect PTT event forces dekey or TX lockout'
