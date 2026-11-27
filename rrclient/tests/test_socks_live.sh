#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# This harness owns a Mongoose event manager and drives MG_EV_* callbacks.
# Native SOCKS coverage is provided by the native connection integration suite.
if ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h"; then
   echo 'SKIP (Mongoose-only): legacy event manager SOCKS fixture'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source rrclient/tests/irc-adapter.sh
${CC:-cc} -Wall -Wextra ${RR_WARNING_FLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} -Iext/libmongoose -DUSE_MONGOOSE=1 \
   rrclient/tests/socks_live.c rrclient/tests/client_stubs.c rrclient/connman.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -lmongoose $(pkg-config --libs mbedtls mbedcrypto mbedx509) ${LDFLAGS:-} -o "$work/socks_live"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=target.invalid \
   -keyout "$work/key.pem" -out "$work/cert.pem" >/dev/null 2>&1
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   python3 rrclient/tests/socks_live.py "$work/socks_live" "$work/cert.pem" "$work/key.pem"

LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   python3 rrclient/tests/socks_fail_closed.py "$work/socks_live"
