#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists librustyscript; then
    echo 'SKIP: RustyScript SDK is required for lifecycle test'
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -Wall -Wextra ${RR_WARNING_FLAGS:-} -Iinc \
    rrclient/tests/script_lifecycle.c \
    $(pkg-config --cflags --libs librustyscript) \
    -o "$work/script_lifecycle"
"$work/script_lifecycle"
