#!/bin/bash
# A bash wrapper to invoke make with some good defaults for parallel build
: "${NCPU:=$(getconf _NPROCESSORS_ONLN 2>/dev/null \
          || getconf NPROCESSORS_ONLN 2>/dev/null \
          || nproc 2>/dev/null \
          || sysctl -n hw.ncpu 2>/dev/null \
          || echo 1)}"

# Ensure we stop on errors
set -euo pipefail

make distclean
make -j $NCPU world
