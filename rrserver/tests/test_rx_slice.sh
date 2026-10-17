#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I. \
   rrserver/rx.slice.c rrserver/tests/rx_slice.c -o "$bin"
"$bin"
