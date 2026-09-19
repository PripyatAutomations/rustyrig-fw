#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   rrclient/tests/status_line.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol -o "$work/status_line"
"$work/status_line"
