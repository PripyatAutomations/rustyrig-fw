#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

profile=${PROFILE:-rrserver}
config="build/$profile/build_config.h"
if [[ ! -f $config ]]; then
   make PROFILE="$profile" buildconf
fi
if [[ ! -f $config ]]; then
   echo "FAIL: missing generated build configuration: $config" >&2
   exit 1
fi
if ! grep -Eq '^[[:space:]]*#define[[:space:]]+USE_SQLITE([[:space:]]|$)' "$config"; then
   echo 'SKIP: resource privilege database test requires SQLite'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I"build/$profile" -Iinc \
   $(pkg-config --cflags glib-2.0) \
   rrserver/tests/resource_privileges.c rrserver/database.privileges.c \
   -lsqlite3 -o "$work/resource-privileges"
"$work/resource-privileges"
