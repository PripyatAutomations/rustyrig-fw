#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} $(pkg-config --cflags glib-2.0) \
   -ffunction-sections -fdata-sections rrserver/tests/usage.c rrserver/usage.c \
   -Wl,--gc-sections -o "$work/usage"
"$work/usage"
