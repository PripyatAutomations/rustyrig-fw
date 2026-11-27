#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Wno-unused-function -Iinc \
  $(pkg-config --cflags glib-2.0) rrclient/tests/reset_files.c \
  $(pkg-config --libs glib-2.0) -o "$bin"
"$bin"
