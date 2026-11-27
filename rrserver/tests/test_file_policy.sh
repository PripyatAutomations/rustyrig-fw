#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu2x -Wall -Wextra ${RR_WARNING_FLAGS:-} -Iinc \
   rrserver/tests/file_policy.c -o "$work/file-policy"
"$work/file-policy"
