#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -DUSE_SQLITE -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags glib-2.0) \
   rrserver/tests/resource_privileges.c rrserver/database.privileges.c \
   -lsqlite3 -o "$work/resource-privileges"
"$work/resource-privileges"
