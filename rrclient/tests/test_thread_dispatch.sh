#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp)
trap 'rm -f "$out"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -I. \
  $(pkg-config --cflags glib-2.0) \
  rrclient/thread.dispatch.c rrclient/tests/thread_dispatch.c \
  $(pkg-config --libs glib-2.0) -o "$out"
"$out"
