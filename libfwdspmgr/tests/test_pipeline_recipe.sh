#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists glib-2.0; then
   echo "SKIP: GLib development package missing"
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. \
   libfwdspmgr/pipeline.recipe.c libfwdspmgr/tests/pipeline_recipe.c \
   $(pkg-config --cflags --libs glib-2.0) -o "$work/recipe"
"$work/recipe"
