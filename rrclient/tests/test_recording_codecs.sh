#!/usr/bin/env bash
# Compile the codec layer independently of the GTK/frontend application.
set -euo pipefail
cd "$(dirname "$0")/../.."
deps=(glib-2.0 flac vorbisenc vorbisfile ogg)
if ! pkg-config --exists "${deps[@]}"; then
   echo "Missing native codec development dependencies: ${deps[*]}" >&2
   exit 1
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=gnu23 -Wall -Wextra -Werror -I. \
   $(pkg-config --cflags "${deps[@]}") \
   rrclient/tests/recording_codecs.c rrclient/recordings.codec.c \
   $(pkg-config --libs "${deps[@]}") -lm -o "$tmp/recording-codecs"
"$tmp/recording-codecs" "$tmp"
