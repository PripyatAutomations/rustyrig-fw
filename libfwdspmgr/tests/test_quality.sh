#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} libfwdspmgr/tests/quality.c -L. -Wl,-rpath,"$PWD" -lfwdspmgr -lrrprotocol -lrustyaxe -o "$work/quality"
"$work/quality"
