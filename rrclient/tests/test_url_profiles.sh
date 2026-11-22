#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
  $(pkg-config --cflags glib-2.0) rrclient/tests/url_profiles.c \
  -lrrprotocol -lrustyaxe $(pkg-config --libs glib-2.0) -o "$work/url_profiles"
"$work/url_profiles"
