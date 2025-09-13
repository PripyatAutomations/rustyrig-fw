#!/bin/bash
set -e

echo "Stopping rrloop (pulse) devices!"
pactl list short modules | grep rrloop | cut -f 1 | while read line; do pactl unload-module "$line"; done

