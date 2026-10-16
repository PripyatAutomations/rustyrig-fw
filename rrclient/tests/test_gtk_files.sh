#!/usr/bin/env bash
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then echo 'SKIP: GTK files requires GTK3'; exit 0; fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags gtk+-3.0) \
   rrclient/tests/gtk_files.c rrclient/files.c rrserver/files.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe $(pkg-config --libs gtk+-3.0) -o "$work/files"
"$work/files"
