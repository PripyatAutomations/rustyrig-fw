#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} ${CFLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/sercom.c rrclient/tests/client_stubs.c rrclient/serial.c rrclient/ws.serial.c rrclient/sercom.c \
   rrclient/cat.c rrclient/cat.yaesu.c rrclient/cat.pty.c \
   $(pkg-config --cflags --libs glib-2.0) -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/sercom"
# rr_sercom_init() cleans stale tty* links in $HOME/dev. Never touch a developer\'s live virtual serial ports.\nHOME="$work" "$work/sercom" "$work"
