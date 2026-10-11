#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
for command in setfacl getfacl sqlite3; do
  command -v "$command" >/dev/null || { echo "SKIP: permissions test requires $command"; exit 0; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/var/lib/rustyrig/db" "$work/var/log/rustyrig"
chmod 700 "$work/var/lib/rustyrig/db" "$work/var/log/rustyrig"
: > "$work/outside"
chmod 600 "$work/outside"
ln -s "$work/outside" "$work/var/log/rustyrig/link"
sqlite3 "$work/var/lib/rustyrig/db/master.db" 'create table fixture(value);'
: > "$work/var/log/rustyrig/old.log"
chmod 600 "$work/var/log/rustyrig/old.log"
# Numeric ACL groups exercise the real helper without adding host accounts.
sed -e 's/rradmin/60001/g' -e 's/rustyrig:/60002:/g' packaging/admin-permissions.sh > "$work/permissions.sh"
sh "$work/permissions.sh" "$work"
sh "$work/permissions.sh" "$work"
python3 - "$work" <<'PY'
import os, pathlib, stat, subprocess, sys
root = pathlib.Path(sys.argv[1])
for directory in (root/'var/lib/rustyrig', root/'var/lib/rustyrig/db', root/'var/log/rustyrig'):
    assert stat.S_IMODE(directory.stat().st_mode) & 5 == 5
    acl = subprocess.check_output(['getfacl', '-cpn', str(directory)], text=True)
    assert 'group:60001:rwx' in acl and 'default:group:60001:rwx' in acl
for file in (root/'var/lib/rustyrig/db/master.db', root/'var/log/rustyrig/old.log'):
    assert stat.S_IMODE(file.stat().st_mode) & 7 == 0
    acl = subprocess.check_output(['getfacl', '-cpn', str(file)], text=True)
    assert 'group:60001:rw-' in acl and 'group:60002:rw-' in acl
assert stat.S_IMODE((root/'outside').stat().st_mode) == 0o600
os.umask(0o077)
new = root/'var/log/rustyrig/new.log'
new.write_text('new log')
acl = subprocess.check_output(['getfacl', '-cpn', str(new)], text=True)
assert 'group:60001:rwx' in acl and 'mask::rw-' in acl and 'other::---' in acl
PY
sqlite3 "$work/var/lib/rustyrig/db/master.db" 'insert into fixture values(1); vacuum;'
echo 'PASS: browseable directories, private existing/new files, administrator ACLs, SQLite maintenance and symlink isolation'
