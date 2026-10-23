#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections $(pkg-config --cflags glib-2.0) \
   rrclient/tests/audio_routes.c rrclient/tests/client_stubs.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe $(pkg-config --cflags --libs glib-2.0) -lrrprotocol ${LDFLAGS:-} -o "$work/audio-routes"
"$work/audio-routes"
