#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/socks_live.c rrclient/connman.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/socks_live"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=target.invalid \
   -keyout "$work/key.pem" -out "$work/cert.pem" >/dev/null 2>&1
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   python3 rrclient/tests/socks_live.py "$work/socks_live" "$work/cert.pem" "$work/key.pem"

LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   python3 rrclient/tests/socks_fail_closed.py "$work/socks_live"
