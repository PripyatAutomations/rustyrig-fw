#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -Wall -Wextra ${RR_WARNING_FLAGS:-} -I. \
   rrclient/tests/wire_sag.c -lm -o "$work/wire-sag"
"$work/wire-sag"
