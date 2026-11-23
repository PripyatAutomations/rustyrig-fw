#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc \
  $(pkg-config --cflags glib-2.0) \
  rrclient/audio.logic.c rrclient/control.graph.c rrclient/control.graph.codec.c \
  rrclient/tests/control_graph_codec.c \
  $(pkg-config --libs glib-2.0) -o "$bin"
"$bin"
