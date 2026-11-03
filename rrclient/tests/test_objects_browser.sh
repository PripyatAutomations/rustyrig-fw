#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ ! -f www/tests/objects.js ]]; then
  echo 'SKIP: legacy browser object tests removed with the web frontend'
  exit 0
fi
node www/tests/objects.js
