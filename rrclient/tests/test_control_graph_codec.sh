#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc \
  $(pkg-config --cflags glib-2.0) \
  odec.c \
  rrclient/tests/control_graph_codec.c \
  $(pkg-config --libs glib-2.0) $(pkg-config --cflags --libs librustyscript) -o "$bin"
"$bin"
