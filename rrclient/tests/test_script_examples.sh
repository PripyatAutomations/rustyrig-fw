#!/usr/bin/env bash
# Smoke-test the user-visible RustyScript example against the installed VM.
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists librustyscript; then
   echo "SKIP: librustyscript development package not installed"
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -Wall -Wextra ${RR_WARNING_FLAGS:-} -O2 \
    rrclient/tests/script_examples.c -o "$work/script_examples" \
    $(pkg-config --cflags --libs librustyscript)
"$work/script_examples" doc/examples/scripts/ascii-art.rrs
