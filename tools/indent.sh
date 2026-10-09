#!/usr/bin/env bash
# Format tracked, owned C sources, including frontend and test subdirectories.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
command -v uncrustify >/dev/null
mapfile -d '' files < <(git ls-files -z -- '*.c' '*.h')
for component in librustyaxe librrprotocol callsign-lookup; do
   while IFS= read -r -d '' file; do
      files+=("$component/$file")
   done < <(git -C "$component" ls-files -z -- '*.c' '*.h')
done
for file in "${files[@]}"; do
   case "$file" in
      ext/*|www/*|debian/*|build/*|snippets/*) continue ;;
   esac
   [[ -f "$file" ]] || continue
   uncrustify -q -c "$root/.uncrustify.cfg" -l C --replace --no-backup "$file"
done
