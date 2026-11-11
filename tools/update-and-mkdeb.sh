#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git pull --ff-only
./tools/update-optional-sources.sh

bash packaging/tests/test_debian_conffiles.sh
dpkg-buildpackage -us -uc -b
