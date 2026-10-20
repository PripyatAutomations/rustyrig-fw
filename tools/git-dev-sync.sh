#!/usr/bin/env bash
# Synchronize a developer checkout without detaching first-party submodule branches.
# Publishing and pinning component commits belongs to git-push-all.sh.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
components=(librustyaxe librrprotocol www callsign-lookup)

cd "$root"
if [[ -z "$(git symbolic-ref -q --short HEAD)" ]]; then
   echo "ERROR: parent repository has a detached HEAD; switch to your development branch." >&2
   exit 1
fi

# Refuse to start if any first-party submodule has uncommitted changes or
# a detached HEAD. Never reset, stash, rebase or move someone's active work.
for component in "${components[@]}"; do
   if ! git -C "$component" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
      echo "ERROR: $component is not initialized; initialize it before dev sync." >&2
      exit 1
   fi
   if [[ -z "$(git -C "$component" symbolic-ref -q --short HEAD)" ]]; then
      echo "ERROR: $component has a detached HEAD. Switch to its development branch first." >&2
      exit 1
   fi
   if [[ -n "$(git -C "$component" status --porcelain)" ]]; then
      echo "ERROR: $component has local changes. Commit or resolve them before syncing." >&2
      exit 1
   fi
done

echo "SYNC: rustyrig-fw (fast-forward only)"
git pull --ff-only
git submodule sync --recursive

for component in "${components[@]}"; do
   echo "SYNC: $component (preserving $(git -C "$component" symbolic-ref --short HEAD))"
   git -C "$component" pull --ff-only
done

echo
echo "Development branches synchronized. No submodule branches detached or reset."
echo "Third-party submodules (ext/*) were not advanced."
echo "The parent may show updated submodule gitlinks until git-push-all.sh publishes them."
git status --short
git submodule status -- "${components[@]}"
