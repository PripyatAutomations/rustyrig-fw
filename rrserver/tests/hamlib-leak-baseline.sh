#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -g -O0 -fsanitize=address -fno-omit-frame-pointer \
   $(pkg-config --cflags hamlib) rrserver/tests/hamlib_leak_baseline.c \
   $(pkg-config --libs hamlib) -o "$work/hamlib-leak-baseline"
echo 'Standalone libhamlib baseline (no RustyRig modules or event loop):'
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$work/hamlib-leak-baseline"
