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

if [[ "$mode" == pinned ]]; then
    git pull --ff-only
    git submodule sync --recursive
    git submodule update --init --recursive
    echo "Checked out parent-pinned submodule revisions:"
    git submodule status
    exit 0
fi

# Development: keep first-party repos on real local branches, recovering
# detached submodule checkouts rather than creating more detached HEADs.
"$P/tools/git-dev-sync.sh"
components=(librustyaxe librrprotocol callsign-lookup)
third_party=()
while read -r _ path; do
    case " ${components[*]} " in
        *" $path "*) ;;
        *) third_party+=("$path") ;;
    esac
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$')
if (( ${#third_party[@]} )); then
    git submodule update --init --recursive -- "${third_party[@]}"
fi
git add -- "${components[@]}"

echo "Staged first-party gitlink pin changes (review and commit in rustyrig-fw):"
git diff --cached --submodule=short -- "${components[@]}"
echo "Third-party submodules remain pinned by the parent repository."
