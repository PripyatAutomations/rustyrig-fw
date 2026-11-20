#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! grep -q '^#define USE_SQLITE' "build/${PROFILE:-rrserver}/build_config.h" || ! command -v openssl >/dev/null; then
   echo 'SKIP: webcam live test requires SQLite and OpenSSL'
   exit 0
fi
if ! command -v gst-inspect-1.0 >/dev/null; then
   echo 'SKIP: webcam live test requires GStreamer tools'
   exit 0
fi
for plugin in x264enc h264parse avdec_h264 jpegenc videorate; do
   if ! gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
      echo "SKIP: webcam live test requires GStreamer $plugin"
      exit 0
   fi
done
python3 rrserver/tests/webcam_live.py
