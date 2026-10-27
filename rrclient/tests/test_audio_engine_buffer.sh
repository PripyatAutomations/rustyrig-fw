#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -Irrclient \
   $(pkg-config --cflags glib-2.0 gthread-2.0) \
   rrclient/tests/audio_engine_buffer.c \
   $(pkg-config --libs glib-2.0 gthread-2.0) ${LDFLAGS:-} \
   -o "$work/audio-engine-buffer"
"$work/audio-engine-buffer"
