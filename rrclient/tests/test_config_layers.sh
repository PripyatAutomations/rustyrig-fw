#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
  -ffunction-sections -fdata-sections rrclient/tests/config_layers.c \
  -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" -lrustyaxe -lrrprotocol -o "$work/config_layers"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/config_layers"
