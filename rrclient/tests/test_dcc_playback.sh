#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -std=gnu11 -ffunction-sections -fdata-sections -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/dcc_playback.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyirc -lrustyaxe $(pkg-config --cflags --libs glib-2.0) ${LDFLAGS:-} -o "$work/dcc-playback"
"$work/dcc-playback"
