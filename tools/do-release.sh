#!/bin/bash
set -euo pipefail

P=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
commit_message=${1-}

"${P}/tools/bump-version.sh"
"${P}/tools/git-push-all.sh"
