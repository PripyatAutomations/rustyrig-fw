#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -Wall -Wextra -Werror -Dsend=rr_test_send -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/irc_sendq.c rrclient/irc.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyirc -lrustyaxe ${LDFLAGS:-} -o "$work/irc_sendq"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/irc_sendq"
