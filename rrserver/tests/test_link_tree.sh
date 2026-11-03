#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrserver/tests/link_tree.c rrserver/link.tree.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe $(pkg-config --cflags --libs glib-2.0) ${LDFLAGS:-} -o "$work/link_tree"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/link_tree"
