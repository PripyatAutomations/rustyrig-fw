#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   rrclient/tests/completion.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol -o "$work/completion"
"$work/completion"
