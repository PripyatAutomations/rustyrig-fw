#!/bin/sh

# Replace header names in #includes throughout the tree
# TODO: eventually take proper arguments and verify header existence

find . -type f \( -name '*.c' -o -name '*.h' \) \
 | sort -u \
 | xargs sed -i 's|^#include "\(librustyaxe/[^"]*\.h\)"|#include <\1>|'
