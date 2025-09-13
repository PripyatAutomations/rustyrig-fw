#!/bin/bash
# XXX: this isn't ready for use yet
set -e

if [ -z "$2" ]; then
   echo "usage: $0 [old-symbol] [new-symbol]"
   exit 1
fi

for i in fwdsp rrclient rrserver; do
   find ./inc/common ./src/common ./inc/$i ./$i \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-rename-19 \
     -qualified-name="$1" \
     -new-name="$2" \
     -i \
     -p ./src/$i
done
