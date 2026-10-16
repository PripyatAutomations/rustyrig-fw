#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
command -v jq >/dev/null || { echo 'SKIP: optional module checks need jq'; exit 0; }
real_pkg_config=$(command -v pkg-config)
cat > "$work/pkg-config" <<EOF_PROXY
#!/usr/bin/env bash
if [[ \$1 == --exists && \$2 == hamlib ]]; then exit 1; fi
exec '$real_pkg_config' "\$@"
EOF_PROXY
chmod +x "$work/pkg-config"
jq '.backend.hamlib = false' config/radio.config.json > "$work/disabled.json"
jq '.backend.hamlib = true' config/radio.config.json > "$work/enabled.json"
status_rule='optional-status:;@echo $(OPTIONAL_MAKE_FLAGS) $(OPTIONAL_DH_EXCLUDES)'
# A disabled SDK must not prevent native or Debian builds, even with old binaries.
result=$(make --no-print-directory -s CF="$work/disabled.json" PKG_CONFIG="$work/pkg-config" --eval "$status_rule" optional-status)
[[ $result == *USE_HAMLIB=false* && $result == *-Nrustyrig-server-hamlib* ]]
plan=$(make --no-print-directory -sf debian/rules -n binary CF="$work/disabled.json" PKG_CONFIG="$work/pkg-config")
[[ $plan == *'dh binary -Nrustyrig-server-hamlib'* ]]
plan=$(make --no-print-directory -sf debian/rules -n override_dh_auto_install CF="$work/disabled.json" PKG_CONFIG="$work/pkg-config")
[[ $plan != *'install -Dm755 bin/rrserver-hamlib.so'* ]]
# An enabled module missing its SDK must stop before either build starts.
if make -s CF="$work/enabled.json" PKG_CONFIG="$work/pkg-config" --eval "$status_rule" optional-status >"$work/native.log" 2>&1; then
   echo 'FAIL: native build silently omitted a configured module'; exit 1
fi
grep -q 'sudo apt install libhamlib-dev' "$work/native.log"
grep -q 'Stopping before building an incomplete package set' "$work/native.log"
if make -sf debian/rules -n binary CF="$work/enabled.json" PKG_CONFIG="$work/pkg-config" >"$work/debian.log" 2>&1; then
   echo 'FAIL: Debian build silently omitted a configured module'; exit 1
fi
grep -q 'sudo apt install libhamlib-dev' "$work/debian.log"
# Explicit opt-outs remain useful without editing a profile for one build.
result=$(make --no-print-directory -s CF="$work/enabled.json" PKG_CONFIG="$work/pkg-config" WITH_HAMLIB=no --eval "$status_rule" optional-status)
[[ $result == *USE_HAMLIB=false* ]]
if pkg-config --exists hamlib; then
   result=$(make --no-print-directory -s CF="$work/enabled.json" --eval "$status_rule" optional-status)
   [[ $result == *USE_HAMLIB=true* && $result != *-Nrustyrig-server-hamlib* ]]
   plan=$(make --no-print-directory -sf debian/rules -n override_dh_auto_install CF="$work/enabled.json")
   [[ $plan == *'install -Dm755 bin/rrserver-hamlib.so debian/rustyrig-server-hamlib/'* ]]
fi
echo 'PASS: profile-selected module packages, missing-SDK preflight hints and disabled/stale-artifact exclusions'
