#!/usr/bin/env bash
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then echo 'SKIP: remote editor requires GTK3'; exit 0; fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -ffunction-sections -fdata-sections -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags gtk+-3.0) \
   rrclient/tests/gtk_remote_editor.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe $(pkg-config --libs gtk+-3.0) -o "$work/editor"
"$work/editor"
