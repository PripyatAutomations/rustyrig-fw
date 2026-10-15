#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0 || ! command -v xvfb-run >/dev/null; then
   echo 'SKIP: GTK tabs requires GTK3 and Xvfb'; exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
test_flags=()
if pkg-config --exists xtst x11; then
   test_flags=(-DHAVE_XTEST $(pkg-config --cflags --libs xtst x11))
fi
${CC:-cc} -I. $(pkg-config --cflags gtk+-3.0) rrclient/tests/gtk_tabs.c \
   $(pkg-config --libs gtk+-3.0) "${test_flags[@]}" -lm -o "$work/tabs"
G_DEBUG=fatal-warnings xvfb-run -a "$work/tabs"
