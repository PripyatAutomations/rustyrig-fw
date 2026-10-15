#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags glib-2.0) \
   rrclient/tests/receiver_connect.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   $(pkg-config --libs glib-2.0) -o "$work/receiver-connect"
"$work/receiver-connect"
