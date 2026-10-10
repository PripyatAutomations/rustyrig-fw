#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. fwdsp/tests/quality.c $(pkg-config --cflags --libs gstreamer-1.0) -o "$work/quality"
"$work/quality"
