#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
message=${1-}
git add -u -- . ':(exclude)ext'
if ! git diff --cached --quiet; then
   if [[ -n "$message" ]]; then git commit -m "$message"; else git commit; fi
fi
git push
