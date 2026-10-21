#!/bin/bash
set -euo pipefail

P=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$P"

# Developer mode advances first-party components and stages their gitlink pins.
# --pinned restores the exact revisions recorded by the parent (CI/release).
mode=advance
case "${1:-}" in
    --pinned) mode=pinned; shift ;;
    --advance|'') [[ $# == 0 ]] || shift ;;
    -h|--help)
        echo "Usage: $0 [--advance|--pinned]"
        echo "  --advance (default): update first-party submodules and stage gitlink pins"
        echo "  --pinned: restore revisions recorded in the parent repository"
        exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
esac
if (( $# )); then echo "Unexpected arguments: $*" >&2; exit 2; fi

# Don't overwrite ongoing work in the parent or any component we advance.
components=(librustyaxe librrprotocol callsign-lookup www)
if [[ "$mode" == advance ]]; then
    for path in "${components[@]}"; do
        if [[ -d "$path" && -n "$(git -C "$path" status --porcelain --untracked-files=normal)" ]]; then
            echo "Dirty submodule $path; commit/stash changes first (or run --pinned)." >&2
            exit 1
        fi
    done
fi

git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive

if [[ "$mode" == pinned ]]; then
    echo "Checked out parent-pinned submodule revisions:"
    git submodule status
    exit 0
fi

for path in "${components[@]}"; do
    # Follow each repository's origin default branch. The protocol repository
    # uses master; don't assume all first-party repositories use the same name.
    git -C "$path" fetch origin
    remote_head=$(git -C "$path" symbolic-ref --quiet --short refs/remotes/origin/HEAD || true)
    if [[ -z "$remote_head" ]]; then
        branch=$(git -C "$path" ls-remote --symref origin HEAD | sed -n 's@^ref: refs/heads/\([^[:space:]]*\)[[:space:]]*HEAD$@\1@p')
        [[ -n "$branch" ]] || { echo "Cannot determine default branch for $path" >&2; exit 1; }
        remote_head="origin/$branch"
    fi
    if ! git -C "$path" rev-parse --verify -q "$remote_head^{commit}" >/dev/null; then
        echo "Missing fetched default branch $remote_head for $path" >&2
        exit 1
    fi
    # A submodule pin is a commit, not a branch. Use detached checkout to avoid
    # moving the developer's local branch and never overwrite dirty components.
    git -C "$path" checkout --detach "$remote_head"
    git add -- "$path"
done

echo "Staged first-party gitlink pin changes (review and commit in rustyrig-fw):"
git diff --cached --submodule=short -- "${components[@]}"
echo "Third-party submodules remain pinned by the parent repository."
