#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists libsrtp2 && [[ -z ${SRTP_CFLAGS:-} ]]; then
   echo 'SKIP: direct media integration requires libsrtp2 development headers'
   exit 0
fi
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" ||
   ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP: live direct media requires the SQLite/WebSocket server'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. -Ibuild/${PROFILE:-radio} \
   rrserver/tests/rtp_peer.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/rtp-peer"
if ! "$work/rtp-peer" --supported; then
   echo 'SKIP: direct media transport is disabled in this build'
   exit 0
fi
export RR_RTP_TEST_PEER="$work/rtp-peer"
for listener in dedicated shared shared-plain; do
   RR_LINK_LISTENER="$listener" python3 rrserver/tests/links_live.py
done
