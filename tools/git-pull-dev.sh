#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
git fetch origin
git switch mesh-linking
git pull --ff-only
./tools/update-optional-sources.sh
