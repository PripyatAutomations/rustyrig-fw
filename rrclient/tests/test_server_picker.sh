#!/usr/bin/env bash
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0 || ! command -v xvfb-run >/dev/null; then
   echo 'SKIP: GTK picker needs GTK3 and xvfb-run'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_GTK -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags gtk+-3.0) -ffunction-sections -fdata-sections \
   rrclient/tests/server_picker.c rrclient/tests/client_stubs.c -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol \
   $(pkg-config --libs gtk+-3.0) -o "$work/server_picker"
xvfb-run -a "$work/server_picker"
