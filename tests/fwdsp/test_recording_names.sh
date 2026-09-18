#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/fwdsp/recording_names.py
