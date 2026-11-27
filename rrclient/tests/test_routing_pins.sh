#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu2x -Wall -Wextra ${RR_WARNING_FLAGS:-} -Iinc -I. \
   $(pkg-config --cflags glib-2.0) \
   rrclient/tests/routing_pins.c $(pkg-config --libs glib-2.0) \
   -o "$work/routing-pins"
"$work/routing-pins"
