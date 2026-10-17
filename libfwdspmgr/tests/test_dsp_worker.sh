#!/bin/sh
# Standalone typed DSP worker smoke test; no radio hardware needed.
set -eu
cd "$(dirname "$0")/../.."
out="$(mktemp)"
trap 'rm -f "$out"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -I. \
   libfwdspmgr/dsp-worker.c libfwdspmgr/tests/dsp_worker.c \
   $(pkg-config --cflags --libs glib-2.0) -o "$out"
"$out"
