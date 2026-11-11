#!/bin/bash
# Parallel native build wrapper. Mongoose and EEPROM are opt-in.
set -euo pipefail
cd "$(dirname "$0")"
: "${NCPU:=$(getconf _NPROCESSORS_ONLN 2>/dev/null \
          || nproc 2>/dev/null \
          || sysctl -n hw.ncpu 2>/dev/null \
          || echo 1)}"

if ! pkg-config --exists gstreamer-1.0; then
   echo "Missing GStreamer development files; run ./install-deps.sh" >&2
   ./install-deps.sh
   exit 1
fi

# Fetch Mongoose only for a build that explicitly enables it.
./tools/update-optional-sources.sh
make distclean
# Environment overrides must reach Make as command-line variables, otherwise
# profile defaults override them and could fetch Mongoose without using it.
options=()
[[ -z ${USE_MONGOOSE:-} ]] || options+=("USE_MONGOOSE=$USE_MONGOOSE")
[[ -z ${USE_EEPROM:-} ]] || options+=("USE_EEPROM=$USE_EEPROM")
make -j "$NCPU" "${options[@]}" world
