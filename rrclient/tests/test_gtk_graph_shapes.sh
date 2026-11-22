#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -I. \
    $(pkg-config --cflags gtk+-3.0) \
    rrclient/gtk/gtk.graph.c rrclient/tests/gtk_graph_shapes.c \
    $(pkg-config --libs gtk+-3.0) -lm -o "$bin"
"$bin"
