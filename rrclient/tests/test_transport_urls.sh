#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# This fixture drives Mongoose callback internals directly. Native transport
# gets its own regression coverage; do not compile Mongoose-only test types
# against an OpenSSL/wslay profile.
if ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP (Mongoose-only): callback/URL transport tests require a Mongoose build'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-but-set-variable -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections rrclient/tests/transport_urls.c rrclient/tests/client_stubs.c rrclient/connman.c rrclient/rooms.c rrclient/ui.c rrclient/cmd.misc.c rrclient/cmd.chat.c rrclient/cmd.tabs.c rrclient/cmd.admin.c -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/transport_urls"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/transport_urls"
