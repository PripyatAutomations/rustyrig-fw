#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrserver/tests/serial_tunnel.c rrserver/serial.c rrclient/serial.c rrclient/sercom.c \
   $(pkg-config --cflags --libs glib-2.0) -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -lutil ${LDFLAGS:-} -o "$work/serial_tunnel"
"$work/serial_tunnel" "$work"
