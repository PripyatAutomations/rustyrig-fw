#!/bin/sh
# Create an idempotent PulseAudio / pipewire-pulse null sink for RustyRig.
# Run as the desktop user whose PulseAudio server fwdsp connects to.
set -eu

SINK_NAME=rustyrig_test
SOURCE_NAME="${SINK_NAME}.monitor"

if ! command -v pactl >/dev/null 2>&1; then
    echo "pactl is required (PulseAudio or PipeWire's pipewire-pulse)." >&2
    exit 1
fi
if ! pactl info >/dev/null 2>&1; then
    echo "Cannot reach the user's PulseAudio-compatible server." >&2
    echo "Run this as the audio-session user (not with sudo)." >&2
    exit 1
fi

if pactl list short sinks | awk -v name="$SINK_NAME" '$2 == name { found=1 } END { exit !found }'; then
    echo "Existing sink: $SINK_NAME"
else
    echo "Creating null sink: $SINK_NAME"
    pactl load-module module-null-sink "sink_name=$SINK_NAME" \
        "sink_properties=device.description=RustyRig_Test" >/dev/null
fi

if ! pactl list short sources | awk -v name="$SOURCE_NAME" '$2 == name { found=1 } END { exit !found }'; then
    echo "Null sink exists, but monitor source $SOURCE_NAME is missing." >&2
    exit 1
fi

echo "Playback device: $SINK_NAME"
echo "Capture device:  $SOURCE_NAME"
echo "For GStreamer, use pulsesink device=$SINK_NAME or pulsesrc device=$SOURCE_NAME"
