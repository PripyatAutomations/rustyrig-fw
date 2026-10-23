#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/irc_mongoose.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyirc -lrustyaxe ${LDFLAGS:-} -o "$work/irc_mongoose"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/irc_mongoose"
