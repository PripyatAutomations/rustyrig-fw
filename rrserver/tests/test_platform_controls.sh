#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. -Iinc \
   rrserver/platform.controls.c rrserver/tests/platform_controls.c \
   -o "$work/platform-controls"
"$work/platform-controls"
