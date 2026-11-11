#!/usr/bin/env bash
# Fetch only third-party sources selected by this build.
set -euo pipefail
cd "$(dirname "$0")/.."
profile=${PROFILE:-radio}
config="config/${profile}.config.json"
[[ -f $config ]] || { echo "Missing $config" >&2; exit 1; }
mongoose=${USE_MONGOOSE:-$(jq -r '.features.mongoose // false | tostring' "$config")}
case "$mongoose" in
   true) git submodule update --init ext/libmongoose ;;
   false) ;;
   *) echo "USE_MONGOOSE must be true or false, got: $mongoose" >&2; exit 2 ;;
esac
case "${WITH_FT8:-no}" in
   yes|true|1) git submodule update --init ext/ft8_lib ;;
   no|false|0) ;;
   *) echo "WITH_FT8 must be yes/no or true/false" >&2; exit 2 ;;
esac
