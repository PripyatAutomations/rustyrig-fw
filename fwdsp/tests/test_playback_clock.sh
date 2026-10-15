#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   fwdsp/tests/playback_clock.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe $(pkg-config --cflags --libs gstreamer-app-1.0 gstreamer-check-1.0) \
   -o "$work/playback-clock"
"$work/playback-clock"
