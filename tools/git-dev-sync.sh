#!/usr/bin/env bash
# Synchronize a developer checkout without detaching first-party submodule branches.
# Publishing and pinning component commits belongs to git-push-all.sh.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
components=(librustyaxe librrprotocol callsign-lookup)

cd "$root"
if [[ -z "$(git symbolic-ref -q --short HEAD)" ]]; then
   echo "ERROR: parent repository has a detached HEAD; switch to your development branch." >&2
   exit 1
fi

# Refuse to start if any first-party submodule has uncommitted changes or
# a detached HEAD. Never reset, stash, rebase or move someone's active work.
for component in "${components[@]}"; do
   if ! git -C "$component" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
      git submodule update --init -- "$component"
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
   branch=$(git -C "$component" symbolic-ref -q --short HEAD || true)
   if [[ -z "$branch" ]]; then
      # Git submodule update --init checks out the parent gitlink as a
      # detached HEAD. Recover the upstream default development branch, but
      # never discard a detached commit that is not already on that branch.
      git -C "$component" fetch origin
      upstream=$(git -C "$component" ls-remote --symref origin HEAD |
         sed -n 's@^ref: refs/heads/\([^[:space:]]*\)[[:space:]]*HEAD$@\1@p')
      if [[ -z "$upstream" ]]; then
         echo "ERROR: cannot find upstream default branch for $component." >&2
         exit 1
      fi
      git -C "$component" fetch origin "$upstream"
      detached=$(git -C "$component" rev-parse HEAD)
      target=$(git -C "$component" rev-parse FETCH_HEAD)
      if ! git -C "$component" merge-base --is-ancestor "$detached" "$target"; then
         echo "ERROR: $component detached HEAD $detached is not contained in origin/$upstream." >&2
         echo "Preserving detached commit; inspect it before switching branches." >&2
         exit 1
      fi
      if git -C "$component" show-ref --verify -q "refs/heads/$upstream"; then
         # If an existing local branch contains commits not on origin,
         # refuse rather than silently replacing it.
         local_head=$(git -C "$component" rev-parse "refs/heads/$upstream")
         if ! git -C "$component" merge-base --is-ancestor "$local_head" "$target"; then
            echo "ERROR: local $component/$upstream has divergent commits. Resolve manually." >&2
            exit 1
         fi
         git -C "$component" switch "$upstream"
      else
         git -C "$component" switch --track -c "$upstream" "origin/$upstream"
      fi
      branch=$upstream
      echo "SYNC: $component recovered detached HEAD onto $branch"
   fi
   echo "SYNC: $component (preserving $branch)"
   git -C "$component" pull --ff-only
done

echo
echo "Development branches synchronized. No submodule branches detached or reset."
echo "Third-party submodules (ext/*) were not advanced."
echo "The parent may show updated submodule gitlinks until git-push-all.sh publishes them."
git status --short
git submodule status -- "${components[@]}"
