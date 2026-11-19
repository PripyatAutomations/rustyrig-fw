#!/usr/bin/env bash
# Regression coverage for the GStreamer graph model embedded in GTK.
set -euo pipefail
cd "$(dirname "$0")/../.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

: "${CC:=cc}"
if ! pkg-config --exists librustyaxe; then
   echo "ERROR: librustyaxe development files are required for GTK pipeline JSON tests" >&2
   exit 1
fi
"$CC" -std=gnu23 -Wall -Wextra \
   $(pkg-config --cflags librustyaxe) rrclient/tests/gtk_pipeline_json.c \
   $(pkg-config --libs librustyaxe) -o "$work/pipeline-json"
"$work/pipeline-json"

if ! pkg-config --exists gstreamer-1.0; then
   echo "SKIP: GStreamer graph model tests (gstreamer-1.0 not installed)"
   exit 0
fi
"$CC" -std=gnu23 -Wall -Wextra -I. \
   $(pkg-config --cflags gstreamer-1.0) \
   rrclient/tests/gtk_pipeline_model.c rrclient/gtk/gtk.pipeline-model.c \
   $(pkg-config --libs gstreamer-1.0) -o "$work/pipeline-model"
"$work/pipeline-model"
