#!/bin/bash
set -e
make distclean
make buildconf
make -j 8
