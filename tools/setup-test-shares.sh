#!/bin/sh
# Enable the two station test shares without replacing other server settings.
set -eu
config_path=${1:-/etc/rustyrig/rrserver.cfg}
if [ "$(id -u)" != 0 ]; then
   echo "Run with sudo: sudo $0 [path-to-rrserver.cfg]" >&2
   exit 1
fi
if [ ! -f "$config_path" ]; then
   echo "Server configuration not found: $config_path" >&2
   exit 1
fi
# Do not change ownership or permissions of directories that already exist.
if [ ! -d /home/rustyrig/SHARED ]; then
   install -d -o rustyrig -g rustyrig -m 0770 /home/rustyrig/SHARED
fi
if [ ! -d /var/log/rustyrig ]; then
   install -d -o rustyrig -g rustyrig -m 0770 /var/log/rustyrig
fi
# Preserve existing named exports; add each missing share independently.
logs_missing=true
shared_missing=true
if grep -Eq '^[[:space:]]*\[share:logs\][[:space:]]*([;#].*)?$' "$config_path"; then logs_missing=false; fi
if grep -Eq '^[[:space:]]*\[share:shared\][[:space:]]*([;#].*)?$' "$config_path"; then shared_missing=false; fi
if [ "$logs_missing" = false ] && [ "$shared_missing" = false ]; then
   echo "Named logs/shared shares already exist; existing settings were preserved."
   exit 0
fi
backup_path=$(mktemp "${config_path}.before-shares.XXXXXX")
cp -p "$config_path" "$backup_path"
if [ "$logs_missing" = true ]; then
   cat >> "$config_path" <<'SHARE'

; File sharing: grant files.read to the desired accounts.
[share:logs]
path=/var/log/rustyrig
read=files.read
write=none
SHARE
fi
if [ "$shared_missing" = true ]; then
   cat >> "$config_path" <<'SHARE'

; Grant files.read / files.write to browse and manage shared files.
[share:shared]
path=/home/rustyrig/SHARED
read=files.read
write=files.write
SHARE
fi
printf 'Added missing test shares. Existing settings preserved. Backup: %s\n' "$backup_path"
printf 'Restart rrserver to load the shares, then open Server Files or use /files.\n'
