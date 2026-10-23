#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/dcc.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyirc -lrustyaxe ${LDFLAGS:-} -o "$work/dcc"
"$work/dcc"
