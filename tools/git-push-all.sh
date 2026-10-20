#!/bin/bash
set -euo pipefail

P=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
commit_message=${1-}

"${P}/tools/bump-version.sh"

# Commit and publish components before the parent gitlinks which reference them.
for i in librustyaxe librrprotocol www callsign-lookup; do
   echo "PUSH: $i"
   cd "${P}/${i}"
   if ! git diff --quiet HEAD --; then
      if [[ -n "$commit_message" ]]; then
         git commit -a -m "$commit_message"
      else
         git commit -a
      fi
   fi
   git push
done

cd "$P"
# A gitlink records a particular component commit, not its branch name.
git add -u -- . ':(exclude)ext'
if ! git diff --cached --quiet; then
   if [[ -n "$commit_message" ]]; then
      git commit -m "$commit_message"
   else
      git commit
   fi
fi
git push

echo "Published parent with pinned component revisions:"
git submodule status -- librustyaxe librrprotocol www callsign-lookup
