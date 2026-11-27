#!/bin/sh
# Package-maintained access for service state. An optional root supports fixtures.
set -e
data="${1:-}/var/lib/rustyrig"
root=${1:-}
for tree in "$data" "$root/var/log/rustyrig"; do
    [ -d "$tree" ] || continue
    # Private identity and immutable packaged resources are not shared
    # administrative state. Never follow symlinks during traversal.
    find "$tree" \( -path "$data/identity" -o -path "$data/sql" -o -path "$data/help" -o -path "$data/tools" \) -prune -o -type d -exec chmod o+rx {} +
    find "$tree" \( -path "$data/identity" -o -path "$data/sql" -o -path "$data/help" -o -path "$data/tools" \) -prune -o -type d -exec setfacl -m g:rustyrig:rwx,g:rradmin:rwx,d:u::rwx,d:g::rwx,d:g:rustyrig:rwx,d:g:rradmin:rwx,d:m::rwx,d:o::--- {} +
    find "$tree" \( -path "$data/identity" -o -path "$data/sql" -o -path "$data/help" -o -path "$data/tools" \) -prune -o -type f -exec chmod o-rwx {} +
    find "$tree" \( -path "$data/identity" -o -path "$data/sql" -o -path "$data/help" -o -path "$data/tools" \) -prune -o -type f -exec setfacl -m g:rustyrig:rwX,g:rradmin:rwX {} +
done
# Repair ACLs left by older packages, including inherited default ACLs.
# These trees contain shipped resources, not live databases or credentials.
for tree in "$data/sql" "$data/help" "$data/tools"; do
    [ -d "$tree" ] && [ ! -L "$tree" ] || continue
    find "$tree" -type d -exec setfacl -b -k {} +
    find "$tree" -type d -exec chmod 0755 {} +
    find "$tree" -type f -exec setfacl -b {} +
    find "$tree" -type f -exec chmod u+rw,go+r,go-w {} +
done
# SQL is data, not executable code.
if [ -d "$data/sql" ] && [ ! -L "$data/sql" ]; then
    find "$data/sql" -maxdepth 1 -type f -name 'sqlite.*.sql' -exec chmod 0644 {} +
fi
