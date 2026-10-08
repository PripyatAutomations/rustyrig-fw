#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$ROOT"
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -c rrclient/tests/userlist_sessions.c -o "$work/userlist_sessions.o"
${CC:-cc} "$work/userlist_sessions.o" -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -o "$work/userlist_sessions"
"$work/userlist_sessions"
