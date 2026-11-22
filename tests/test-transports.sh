#!/usr/bin/env bash
# Exercise both wire transports in disposable worktrees. Never replace the
# user's build/radio objects or shared bin/ outputs with another profile.
set -euo pipefail
root=$(git -C "$(dirname "$0")/.." rev-parse --show-toplevel)
mode=${1:-both}
case "$mode" in native|mongoose|both) ;; *)
   echo "Usage: tests/test-transports.sh [native|mongoose|both]" >&2; exit 2 ;;
esac
command -v jq >/dev/null || { echo 'Missing jq' >&2; exit 2; }
work=$(mktemp -d)
cleanup() {
   for profile in native mongoose; do
      if [[ -d $work/$profile ]]; then
         git -C "$root" worktree remove --force "$work/$profile" || true
      fi
   done
   rm -rf "$work"
}
trap cleanup EXIT
status=0
run_profile() {
   local profile=$1 checkout="$work/$1"
   git -C "$root" worktree add --detach "$checkout" HEAD
   (
      cd "$checkout"
      if [[ $profile == mongoose ]]; then
         jq '.features.mongoose = "true" | .debug.mongoose = "true"' config/radio.config.json > config/transport-mongoose.config.json
         # Mongoose remains opt-in. A missing source is a build failure,
         # not a reason to silently skip the transport.
         PROFILE=transport-mongoose USE_MONGOOSE=true WITH_FT8=no ./tools/update-optional-sources.sh
         make PROFILE=transport-mongoose test
      else
         make PROFILE=radio test
      fi
   )
}
if [[ $mode == native || $mode == both ]]; then
   echo '=== Native OpenSSL/wslay full test suite ==='
   run_profile native || status=1
fi
if [[ $mode == mongoose || $mode == both ]]; then
   echo '=== Mongoose full test suite ==='
   run_profile mongoose || status=1
fi
exit "$status"
