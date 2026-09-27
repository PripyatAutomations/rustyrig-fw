#!/bin/bash
set -euo pipefail

P=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
commit_message=${1-}

"${P}/tools/bump-version.sh"

for i in librustyaxe librrprotocol www callsign-lookup .; do
   cd "${P}/${i}"
   echo "PUSH: $i"
   if [ -z "$commit_message" ]; then
      git commit -a
   else
      git commit -a -m "${commit_message}"
   fi
   git push
   cd "${P}"
done
