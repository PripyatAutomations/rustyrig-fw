#!/bin/bash
set -e

# Create directories distclean removed
mkdir -p audit-logs build db
make distclean
make -j 8 world
