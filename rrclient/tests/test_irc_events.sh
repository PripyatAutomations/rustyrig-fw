#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/irc_events.c rrclient/m_privmsg.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/irc_events"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/irc_events"
