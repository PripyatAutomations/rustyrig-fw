#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu2x -Wall -Wextra ${RR_WARNING_FLAGS:-} -I. \
    $(pkg-config --cflags gtk+-3.0 librustyui) \
    rrclient/gtk/gtk.graph.c rrclient/tests/gtk_graph_shapes.c \
    $(pkg-config --libs gtk+-3.0 librustyui) -lm -o "$bin"
"$bin"
