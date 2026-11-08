#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
echo "Updated rustyrig-fw and optional third-party dependencies."
