#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0 || ! command -v xvfb-run >/dev/null; then
   echo 'SKIP: GTK icons requires GTK3 and Xvfb'; exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wno-deprecated-declarations -I. $(pkg-config --cflags gtk+-3.0) \
   rrclient/tests/gtk_icons.c $(pkg-config --libs gtk+-3.0) -o "$work/icons"
G_DEBUG=fatal-warnings xvfb-run -a "$work/icons"
