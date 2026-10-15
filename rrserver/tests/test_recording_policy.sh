#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrserver/tests/recording_policy.c rrserver/defconfig.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/recording-policy"
"$work/recording-policy"
