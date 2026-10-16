#!/usr/bin/env bash
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then
   echo 'SKIP: GTK directory browser needs GTK development headers'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -DUSE_GTK=1 -Wno-deprecated-declarations -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags gtk+-3.0 libcurl sqlite3) rrclient/tests/gtk_receiver_directory.c \
   -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol \
   $(pkg-config --libs gtk+-3.0 libcurl sqlite3) -o "$work/gtk-directory"
if command -v xvfb-run >/dev/null 2>&1 && [[ -z "${DISPLAY:-}" ]]; then
   xvfb-run -a "$work/gtk-directory" "$work/cache.sqlite3"
else
   "$work/gtk-directory" "$work/cache.sqlite3"
fi
