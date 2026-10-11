#!/bin/sh
# Package-maintained access for service state. An optional root supports fixtures.
set -e
root=${1:-}
for tree in "$root/var/lib/rustyrig" "$root/var/log/rustyrig"; do
    [ -d "$tree" ] || continue
    # Physical traversal: do not change targets of links in the state tree.
    find "$tree" -type d -exec chmod o+rx {} +
    find "$tree" -type d -exec setfacl -m g:rustyrig:rwx,g:rradmin:rwx,d:u::rwx,d:g::rwx,d:g:rustyrig:rwx,d:g:rradmin:rwx,d:m::rwx,d:o::--- {} +
    find "$tree" -type f -exec chmod o-rwx {} +
    find "$tree" -type f -exec setfacl -m g:rustyrig:rwX,g:rradmin:rwX {} +
done
