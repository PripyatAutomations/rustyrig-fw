#!/bin/bash
git pull --ff-only
git submodule update --init --recursive

# Run conffile packaging regression tests
bash packaging/tests/test_debian_conffiles.sh

# Build packages
dpkg-buildpackage -us -uc -b
