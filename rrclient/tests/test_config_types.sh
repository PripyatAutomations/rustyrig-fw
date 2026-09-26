#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   rrclient/tests/config_types.c rrclient/defconfig.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/config_types"
"$work/config_types"
