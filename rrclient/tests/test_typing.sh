#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$ROOT"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/typing.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -o "$work/typing"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/typing"
