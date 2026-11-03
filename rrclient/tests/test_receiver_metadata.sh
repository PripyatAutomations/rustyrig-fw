#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   rrclient/tests/receiver_metadata.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe $(pkg-config --cflags --libs glib-2.0) -lrrprotocol -o "$work/receiver-metadata"
"$work/receiver-metadata"
