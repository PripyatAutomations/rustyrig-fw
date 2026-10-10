#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-but-set-variable -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrclient/tests/transport_urls.c rrclient/connman.c rrclient/rooms.c rrclient/ui.c rrclient/cmd.misc.c rrclient/cmd.chat.c rrclient/cmd.tabs.c -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/transport_urls"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/transport_urls"
