#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/bin" "$work/source/tools" "$work/source/config" "$work/etc/rustyrig"
cp "$ROOT"/tools/get-*-log.sh "$work/source/tools/"
cp "$ROOT"/tools/get-*-log.sh "$work/bin/"
touch "$work/source/GNUmakefile"

cat > "$work/bin/sqlite3" <<'EOF'
#!/bin/sh
printf '%s\n' "$1" > "$CAPTURE"
EOF
chmod +x "$work/bin/sqlite3"

cat > "$work/etc/rustyrig/radio.config.json" <<'EOF'
{"database":{"master":{"path":"/var/lib/rustyrig/db/radio.db"}}}
EOF
cat > "$work/etc/rustyrig/field.config.json" <<'EOF'
{"database":{"master":{"path":"/srv/rustyrig/field.db"}}}
EOF

for script in "$work"/source/tools/get-*-log.sh; do
   capture="$work/capture"
   source_db="$work/source/db/master.db"

   (
      cd "$work/source"
      CAPTURE="$capture" PATH="$work/bin:$PATH" "./tools/$(basename "$script")"
   )
   [[ "$(cat "$capture")" == "$source_db" ]]

   (
      cd "$work/source"
      CAPTURE="$capture" PATH="$work/bin:$PATH" "tools/$(basename "$script")"
   )
   [[ "$(cat "$capture")" == "$source_db" ]]

   (
      cd "$work"
      CAPTURE="$capture" PATH="$work/bin:$PATH" "$script"
   )
   [[ "$(cat "$capture")" == "$source_db" ]]

   (
      cd "$work"
      RUSTYRIG_CONFIG_DIR="$work/etc/rustyrig" CAPTURE="$capture" \
         PATH="$work/bin:$PATH" "$(basename "$script")"
   )
   [[ "$(cat "$capture")" == "/var/lib/rustyrig/db/radio.db" ]]

   (
      cd "$work"
      RUSTYRIG_CONFIG_DIR="$work/etc/rustyrig" CAPTURE="$capture" \
         PATH="$work/bin:$PATH" "$work/bin/$(basename "$script")"
   )
   [[ "$(cat "$capture")" == "/var/lib/rustyrig/db/radio.db" ]]

   (
      cd "$work"
      PROFILE=field RUSTYRIG_CONFIG_DIR="$work/etc/rustyrig" CAPTURE="$capture" \
         PATH="$work/bin:$PATH" "$(basename "$script")"
   )
   [[ "$(cat "$capture")" == "/srv/rustyrig/field.db" ]]
done

make -s -C "$ROOT" install-tools PROFILE=radio INSTALL_DIR="$work/install"
for script in get-audit-log.sh get-chat-log.sh get-ptt-log.sh; do
   [[ -x "$work/install/bin/$script" ]]
done

echo "PASS: log tools distinguish source and installed locations and select profiles"
