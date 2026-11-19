#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc \
   $(pkg-config --cflags glib-2.0) \
   rrserver/tests/catalog_policy.c \
   $(pkg-config --libs glib-2.0) -o "$work/catalog-policy"
"$work/catalog-policy"
