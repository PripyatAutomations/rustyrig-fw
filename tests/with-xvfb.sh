#!/usr/bin/env bash
# Isolate test GUIs from the host X11 and Wayland displays, even on a desktop.
set -euo pipefail
if [[ ${RR_TEST_XVFB:-} == 1 ]]; then
   exec "$@"
fi
if ! command -v xvfb-run >/dev/null 2>&1; then
   echo 'ERROR: tests require xvfb-run; refusing to use the host display' >&2
   exit 1
fi
unset DISPLAY WAYLAND_DISPLAY
export GDK_BACKEND=x11 RR_TEST_XVFB=1
exec xvfb-run -a "$@"
