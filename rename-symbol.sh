#!/bin/bash
set -e

if [ -z "$2" ]; then
   echo "usage: $0 [old-symbol] [new-symbol]"
   exit 1
fi

find ./inc ./src \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-rename-19 \
  -old-name="$1" \
  -new-name="$2" \
  -dry-run \
  -p ./compile_commands.json

#  -i \
#  \( -name '*.c' -o -name '*.h' -o -path '../../inc/common/*.h' -o -path '../common/*.c' \) \
