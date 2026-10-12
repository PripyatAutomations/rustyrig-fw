#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/dcc_audio.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/dcc-audio"
"$work/dcc-audio"
