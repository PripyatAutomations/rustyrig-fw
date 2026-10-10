#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -Wno-unused-parameter -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/transport_urls.c rrclient/connman.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/transport_urls"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/transport_urls"
