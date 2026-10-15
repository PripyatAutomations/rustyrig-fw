#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags glib-2.0 libcurl sqlite3) rrclient/tests/receiver_directory.c \
   -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol \
   $(pkg-config --libs glib-2.0 libcurl sqlite3) -o "$work/directory"
python3 rrclient/tests/receiver_directory_http.py "$work/directory" "$work/cache.sqlite3"
