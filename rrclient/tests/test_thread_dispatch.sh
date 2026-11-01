#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror \
  $(pkg-config --cflags glib-2.0) \
  rrclient/thread.dispatch.c rrclient/tests/thread_dispatch.c \
  $(pkg-config --libs glib-2.0) -o "$work/test-dispatch"
"$work/test-dispatch"
echo "PASS: GLib cross-context command dispatch"
