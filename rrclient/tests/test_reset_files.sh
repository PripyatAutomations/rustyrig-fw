#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu2x -Wall -Wextra ${RR_WARNING_FLAGS:-} -Wno-unused-function -Iinc \
  $(pkg-config --cflags glib-2.0) rrclient/tests/reset_files.c \
  $(pkg-config --libs glib-2.0) -o "$bin"
"$bin"
