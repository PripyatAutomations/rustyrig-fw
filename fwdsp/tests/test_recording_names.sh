#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 fwdsp/tests/recording_names.py
