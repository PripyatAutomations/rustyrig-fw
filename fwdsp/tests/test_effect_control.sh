#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ ! -x bin/fwdsp ]]; then
  echo "SKIP: build bin/fwdsp before running the effect-control IPC test"
  exit 0
fi
python3 fwdsp/tests/effect_control.py
