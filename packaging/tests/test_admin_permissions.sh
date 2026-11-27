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
for resource in sql help tools; do
  mkdir -p "$work/var/lib/rustyrig/$resource"
  chmod 770 "$work/var/lib/rustyrig/$resource"
  setfacl -m g:60001:rwx,d:g:60001:rwx "$work/var/lib/rustyrig/$resource"
  touch "$work/var/lib/rustyrig/$resource/fixture"
  chmod 660 "$work/var/lib/rustyrig/$resource/fixture"
  setfacl -m g:60001:rw- "$work/var/lib/rustyrig/$resource/fixture"
done
mv "$work/var/lib/rustyrig/sql/fixture" "$work/var/lib/rustyrig/sql/sqlite.client.sql"
chmod 770 "$work/var/lib/rustyrig/sql/sqlite.client.sql"
chmod 770 "$work/var/lib/rustyrig/tools/fixture"
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
for name, filename, mode in (
    ('sql', 'sqlite.client.sql', 0o644),
    ('help', 'fixture', 0o644),
    ('tools', 'fixture', 0o754),
):
    directory = root/'var/lib/rustyrig'/name
    file = directory/filename
    assert stat.S_IMODE(directory.stat().st_mode) == 0o755
    assert stat.S_IMODE(file.stat().st_mode) == mode
    for path in (directory, file):
        acl = subprocess.check_output(['getfacl', '-cpn', str(path)], text=True)
        assert 'group:60001:' not in acl and 'group:60002:' not in acl
        assert 'default:' not in acl
assert stat.S_IMODE((root/'outside').stat().st_mode) == 0o600
os.umask(0o077)
new = root/'var/log/rustyrig/new.log'
new.write_text('new log')
acl = subprocess.check_output(['getfacl', '-cpn', str(new)], text=True)
assert 'group:60001:rwx' in acl and 'mask::rw-' in acl and 'other::---' in acl
PY
sqlite3 "$work/var/lib/rustyrig/db/master.db" 'insert into fixture values(1); vacuum;'
echo 'PASS: browseable directories, private existing/new files, administrator ACLs, SQLite maintenance and symlink isolation'
