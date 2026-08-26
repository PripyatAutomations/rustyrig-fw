#!/bin/bash
# A bash wrapper to invoke make with some good defaults for parallel build
: "${NCPU:=$(getconf _NPROCESSORS_ONLN 2>/dev/null \
          || getconf NPROCESSORS_ONLN 2>/dev/null \
          || nproc 2>/dev/null \
          || sysctl -n hw.ncpu 2>/dev/null \
          || echo 1)}"

# Ensure we stop on errors
set -euo pipefail

SUDO=$(which sudo)

# Try to determine if gstreamer dev package installed, if not install deps
X=$(pkg-config --cflags gstreamer-1.0)
if [ $? != 0 ]; then
   $SUDO ./install-deps.sh
fi

# If it looks like submodules are missing, pull them now
if [ ! -f ext/libmongoose/mongoose.c ]; then
   git submodule init
   git submodule update --depth=1
fi

# Clean up the tree
make distclean

# Build with all cores
make -j $NCPU world
