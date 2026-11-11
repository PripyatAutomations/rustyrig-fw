#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
git pull --ff-only
./tools/update-optional-sources.sh
echo "Updated RustyRig and only the enabled optional third-party sources."
