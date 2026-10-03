#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/bin" "$work/tools" "$work/config"
cp "$ROOT"/tools/get-*-log.sh "$work/tools/"

cat > "$work/bin/sqlite3" <<'EOF'
#!/bin/sh
printf '%s\n' "$1" > "$CAPTURE"
EOF
chmod +x "$work/bin/sqlite3"

cat > "$work/config/radio.config.json" <<'EOF'
{"database":{"master":{"path":"/var/lib/rustyrig/db/radio.db"}}}
EOF
cat > "$work/config/field.config.json" <<'EOF'
{"database":{"master":{"path":"/srv/rustyrig/field.db"}}}
EOF

for script in "$work"/tools/get-*-log.sh; do
   capture="$work/capture"

   (
      cd "$work"
      CAPTURE="$capture" PATH="$work/bin:$PATH" "./tools/$(basename "$script")"
   )
   [[ "$(cat "$capture")" == "./db/master.db" ]]

   (
      cd "$work"
      CAPTURE="$capture" PATH="$work/bin:$PATH" "$script"
   )
   [[ "$(cat "$capture")" == "/var/lib/rustyrig/db/radio.db" ]]

   (
      cd "$work"
      PROFILE=field CAPTURE="$capture" PATH="$work/bin:$PATH" "$script"
   )
   [[ "$(cat "$capture")" == "/srv/rustyrig/field.db" ]]
done

echo "PASS: log tools resolve source, default-profile, and selected-profile database paths"
