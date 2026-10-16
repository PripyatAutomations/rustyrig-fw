#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! command -v gst-inspect-1.0 >/dev/null || ! gst-inspect-1.0 x264enc >/dev/null 2>&1; then
   echo 'SKIP: video test requires GStreamer H.264 plugins'; exit 0
fi
for plugin in x264enc h264parse avdec_h264 pnmenc; do
   if ! gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
      echo "SKIP: video test requires GStreamer $plugin"; exit 0
   fi
done
python3 fwdsp/tests/video.py
