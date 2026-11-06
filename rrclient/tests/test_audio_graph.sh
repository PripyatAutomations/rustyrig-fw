#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -Wall -Wextra -I. \
   $(pkg-config --cflags glib-2.0) \
   rrclient/tests/audio_graph.c rrclient/audio.graph.c \
   $(pkg-config --libs glib-2.0) ${LDFLAGS:-} -o "$work/audio-graph"
"$work/audio-graph"
