#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc rrserver/tests/file_transfer_wire.c -o "$work/file-transfer-wire"
"$work/file-transfer-wire"
echo 'PASS: FILE binary codec bounds, transfer UUID, offsets and offer deadline'
