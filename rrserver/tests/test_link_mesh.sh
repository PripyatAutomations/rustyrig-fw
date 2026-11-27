#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra ${RR_WARNING_FLAGS:-} -I. -Iinc \
   rrserver/tests/link_mesh.c rrserver/link.mesh.c -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/link_mesh"
"$work/link_mesh"
