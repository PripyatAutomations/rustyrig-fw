#!/bin/sh
# Package-maintained access for service state. An optional root supports fixtures.
set -e
root=${1:-}
for tree in "$root/var/lib/rustyrig" "$root/var/log/rustyrig"; do
    [ -d "$tree" ] || continue
    # The mesh signing/TLS identity is private trust material, not shared
    # administrative state. Prune the entire subtree before any chmod/ACL.
    # Physical traversal: never follow links in the state tree.
    identity="$root/var/lib/rustyrig/identity"
    find "$tree" -path "$identity" -prune -o -type d -exec chmod o+rx {} +
    find "$tree" -path "$identity" -prune -o -type d -exec setfacl -m g:rustyrig:rwx,g:rradmin:rwx,d:u::rwx,d:g::rwx,d:g:rustyrig:rwx,d:g:rradmin:rwx,d:m::rwx,d:o::--- {} +
    find "$tree" -path "$identity" -prune -o -type f -exec chmod o-rwx {} +
    find "$tree" -path "$identity" -prune -o -type f -exec setfacl -m g:rustyrig:rwX,g:rradmin:rwX {} +
done
