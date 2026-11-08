#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
