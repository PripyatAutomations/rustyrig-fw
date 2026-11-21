#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
if [[ ${RR_TEST_GDB:-0} == 1 ]]; then
   trap 'printf "Kept serial test workspace: %s\\n" "$work"' EXIT
else
   trap 'rm -rf "$work"' EXIT
fi
source rrclient/tests/irc-adapter.sh
${CC:-cc} -g -fno-omit-frame-pointer ${CFLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/sercom.c rrclient/tests/client_stubs.c rrclient/serial.c rrclient/ws.serial.c rrclient/sercom.c \
   rrclient/cat.c rrclient/cat.yaesu.c rrclient/cat.pty.c \
   $(pkg-config --cflags --libs glib-2.0) -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/sercom"
# rr_sercom_init() cleans stale tty* links in $HOME/dev. Never touch a developer\'s live virtual serial ports.\nif [[ ${RR_TEST_GDB:-0} == 1 ]]; then
   HOME="$work" gdb -q -batch -ex run -ex 'thread apply all bt full' --args "$work/sercom" "$work"
else
   HOME="$work" "$work/sercom" "$work"
fi
