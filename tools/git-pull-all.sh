#!/bin/bash
set -euo pipefail

P=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$P"

# The parent pins tested component revisions; never advance them with --remote.
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive

echo "Checked out revisions pinned by rustyrig-fw:"
git submodule status
