#!/bin/bash
set -e

if [ ! -f .installed_deb_deps ]; then
   ./install-deps.sh && touch .installed_deb_deps
fi

dpkg-buildpackage -us -uc -j"${NCPU:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)}"
