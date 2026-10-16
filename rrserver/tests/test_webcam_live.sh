#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-radio}/build_config.h" || ! grep -q '^#define USE_MONGOOSE' "build/${PROFILE:-radio}/build_config.h" || ! command -v gst-inspect-1.0 >/dev/null; then
   echo 'SKIP: webcam live test requires SQLite and GStreamer'; exit 0
fi
for plugin in x264enc h264parse avdec_h264 jpegenc videorate; do
   if ! gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
      echo "SKIP: video test requires GStreamer $plugin"; exit 0
   fi
done
python3 rrserver/tests/webcam_live.py
