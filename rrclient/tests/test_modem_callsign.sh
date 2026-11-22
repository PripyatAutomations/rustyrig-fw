#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
if ! pkg-config --exists glib-2.0; then
   echo "SKIP: glib-2.0 development files missing"
   exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc \
  $(pkg-config --cflags glib-2.0) \
  rrclient/tests/modem_callsign.c -o "$tmp/modem_callsign" \
  $(pkg-config --libs glib-2.0)
"$tmp/modem_callsign"
echo "Modem callsign token tests passed"
