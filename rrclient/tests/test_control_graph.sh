#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu23 -Wall -Wextra ${RR_WARNING_FLAGS:-} -Iinc \
  rrclient/tests/control_graph.c $(pkg-config --cflags --libs librustyscript) -o "$bin"
"$bin"
