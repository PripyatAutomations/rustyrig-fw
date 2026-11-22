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
if pkg-config --exists librustyscript; then
  ${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc \
    $(pkg-config --cflags librustyscript) \
    rrclient/tests/modem_callsign_script.c -o "$tmp/modem_callsign_script" \
    $(pkg-config --libs librustyscript)
  "$tmp/modem_callsign_script" doc/examples/scripts/callsign-alert.rrs
else
  echo "SKIP: RustyScript development package unavailable"
fi
