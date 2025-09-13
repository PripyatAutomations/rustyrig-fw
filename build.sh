#!/bin/bash
set -e

mkdir -p audit-logs build
make distclean
make -j 8 world
