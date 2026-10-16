#!/usr/bin/env bash
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then
   echo 'SKIP: GTK formatting test requires GTK development headers'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_GTK=1 -Wno-deprecated-declarations -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections $(pkg-config --cflags gtk+-3.0) \
   rrclient/tests/gtk_formatting.c rrclient/tests/client_stubs.c rrclient/gtk/gtk.tabs.menu.c rrclient/gtk/gtk.tabs.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol $(pkg-config --libs gtk+-3.0) -o "$work/gtk_formatting"
# The popup grabs entry focus and exercises clipboard ownership. Always use a
# private display when available so desktop typing cannot alter the assertions.
if command -v xvfb-run >/dev/null 2>&1; then
   xvfb-run -a "$work/gtk_formatting"
else
   "$work/gtk_formatting"
fi
