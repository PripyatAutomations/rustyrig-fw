#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -fPIC -shared -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrbridge/tests/mock-driver.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   -o "$work/rrbridge-mock.so"
ln -s "$PWD/bin/rrbridge-kiwi.so" "$work/rrbridge-kiwi.so"
python3 rrserver/tests/bridge_live.py "$work"
