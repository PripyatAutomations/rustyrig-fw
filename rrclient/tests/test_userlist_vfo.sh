#!/usr/bin/env bash
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0; then
   echo 'SKIP: userlist VFO selection test requires GTK development headers'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -DUSE_GTK=1 -Wno-deprecated-declarations -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   $(pkg-config --cflags gtk+-3.0) rrclient/tests/userlist_vfo.c rrclient/tests/client_stubs.c \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol -lrustyirc \
   $(pkg-config --libs gtk+-3.0) ${LDFLAGS:-} -o "$work/userlist_vfo"
"$work/userlist_vfo"
