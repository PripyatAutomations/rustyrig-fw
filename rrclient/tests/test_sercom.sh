#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/sercom.c rrclient/serial.c rrclient/sercom.c \
   rrclient/cat.c rrclient/cat.yaesu.c rrclient/cat.pty.c \
   $(pkg-config --cflags --libs glib-2.0) -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/sercom"
"$work/sercom" "$work"
