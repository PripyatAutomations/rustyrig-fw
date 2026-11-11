#!/usr/bin/env bash
# Prevent regressions that silently clone Mongoose on native-only builds.
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/git" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$FETCH_LOG"
SH
chmod +x "$work/git"
export FETCH_LOG="$work/requests.txt"
PATH="$work:$PATH" PROFILE=radio USE_MONGOOSE=false WITH_FT8=no \
   ./tools/update-optional-sources.sh
test ! -e "$FETCH_LOG"
PATH="$work:$PATH" PROFILE=radio USE_MONGOOSE=true WITH_FT8=no \
   ./tools/update-optional-sources.sh
test "$(cat "$FETCH_LOG")" = 'submodule update --init ext/libmongoose'
: >"$FETCH_LOG"
PATH="$work:$PATH" PROFILE=radio USE_MONGOOSE=false WITH_FT8=yes \
   ./tools/update-optional-sources.sh
test "$(cat "$FETCH_LOG")" = 'submodule update --init ext/ft8_lib'
echo 'PASS: Mongoose and FT8 sources are fetched only on explicit opt-in'
