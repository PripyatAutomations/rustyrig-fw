#!/usr/bin/env bash
# Run complete RustyRig regression suites for both WebSocket implementations.
# Each profile builds from detached worktrees and installs its *own* standalone
# libraries, so Mongoose support and rrconn_t ABI can never be inherited from
# the user's current installed/native dependencies.
set -euo pipefail
root=$(git -C "$(dirname "$0")/.." rev-parse --show-toplevel)
mode=${1:-both}
case "$mode" in native|mongoose|both) ;; *)
   echo "Usage: tests/test-transports.sh [native|mongoose|both]" >&2
   exit 2 ;;
esac
command -v jq >/dev/null || { echo 'Missing jq' >&2; exit 2; }
for source in librustyaxe librrprotocol; do
   if [[ ! -d "$root/../$source/.git" && ! -f "$root/../$source/.git" ]]; then
      echo "FAIL: complete transport suites require sibling $source git checkout" >&2
      exit 2
   fi
done
work=$(mktemp -d)
children=()
cleanup() {
   local i
   for ((i=${#children[@]}-1; i>=0; --i)); do
      git -C "${children[i]%%::*}" worktree remove --force "${children[i]#*::}" || true
   done
   rm -rf "$work"
}
trap cleanup EXIT
checkout() {
   local repo=$1 path=$2
   git -C "$repo" worktree add --detach "$path" HEAD
   children+=("$repo::$path")
}
run_profile() {
   local backend=$1
   local checkout_path="$work/$backend"
   local axe="$work/$backend-librustyaxe"
   local proto="$work/$backend-librrprotocol"
   local prefix="$work/$backend-prefix"
   local profile=radio
   checkout "$root" "$checkout_path"
   checkout "$root/../librustyaxe" "$axe"
   checkout "$root/../librrprotocol" "$proto"
   mkdir -p "$prefix"
   (
      cd "$checkout_path"
      export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
      export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
      export RR_WIRE_SCHEMA="$proto/wire-schema.json"
      local mongoose=false
      if [[ $backend == mongoose ]]; then
         mongoose=true
         profile=transport-mongoose
         jq '.features.mongoose = "true" | .debug.mongoose = "true"' config/radio.config.json > "config/$profile.config.json"
         PROFILE="$profile" USE_MONGOOSE=true WITH_FT8=no ./tools/update-optional-sources.sh
      fi
      # Build each library with the selected transport adapter and install
      # under the worktree's private prefix. Never modify system SDKs.
      if [[ $mongoose == true ]]; then
         make -C "$axe" WITH_MONGOOSE=true \
            MONGOOSE_SOURCE="$checkout_path/ext/libmongoose/mongoose.c" \
            MONGOOSE_INCLUDE_DIR="$checkout_path/ext/libmongoose" \
            CFLAGS="-DMG_TLS=MG_TLS_MBED" \
            LDFLAGS="$(pkg-config --libs mbedtls mbedcrypto mbedx509)" \
            PREFIX="$prefix" LIBDIR="$prefix/lib" INCLUDEDIR="$prefix/include" test install
      else
         make -C "$axe" WITH_MONGOOSE=false \
            PREFIX="$prefix" LIBDIR="$prefix/lib" INCLUDEDIR="$prefix/include" test install
      fi
      make -C "$proto" PREFIX="$prefix" LIBDIR="$prefix/lib" INCLUDEDIR="$prefix/include" test install
      # Explicitly run every RustyRig-owned component suite, not just the
      # transport tests. Application binaries and their generated headers
      # remain isolated inside the selected RustyRig worktree.
      make PROFILE="$profile" \
         TEST_SUITES="selftest packaging rrserver rrclient rrbridge fwdsp libfwdspmgr" test
   )
}
status=0
if [[ $mode == native || $mode == both ]]; then
   echo '=== Native OpenSSL/wslay full suite (private libraries) ==='
   run_profile native || status=1
fi
if [[ $mode == mongoose || $mode == both ]]; then
   echo '=== Mongoose full suite (private libraries) ==='
   run_profile mongoose || status=1
fi
exit "$status"
