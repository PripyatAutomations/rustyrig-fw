#!/usr/bin/env bash
# Grant a linked-site identity broad demonstration access on THIS rrserver.
# Usage: bash tools/grant-remote-superadmin.sh /path/to/master.db origin-site [username]
# Default identity: admin@origin-site
# WARNING: Development/demo use only. Does not bypass RF/PTT safety or grant
# legacy serial/file privileges stored outside resource_privileges.
set -euo pipefail

usage() {
    echo "Usage: $0 <destination-master.db> <origin-site> [username=admin]" >&2
    exit 2
}
[[ $# -ge 2 && $# -le 3 ]] || usage
db=$1
host=$2
user=${3:-admin}

command -v sqlite3 >/dev/null 2>&1 || { echo "sqlite3 is required" >&2; exit 1; }
[[ -f $db ]] || { echo "Database does not exist: $db" >&2; exit 1; }
# Restrict values interpolated into SQL. This also disallows SQL metacharacters.
[[ $host =~ ^[[:alnum:]_.-]+$ && $user =~ ^[[:alnum:]_.-]+$ ]] || {
    echo "Username and originating site may contain only letters, digits, _, . and -" >&2
    exit 2
}

# Schema migrations run when rrserver starts. Fail instead of silently
# creating a new database or applying partial grants to an old schema.
for table in privilege_sets privilege_set_entries privilege_set_assignments resource_privileges; do
    [[ $(sqlite3 "$db" "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='$table';") == 1 ]] || {
        echo "Missing table $table; initialize/migrate the destination master database first" >&2
        exit 1
    }
done
for table in privilege_set_assignments resource_privileges; do
    [[ $(sqlite3 "$db" "SELECT count(*) FROM pragma_table_info('$table') WHERE name='remote_allowed';") == 1 ]] || {
        echo "Missing $table.remote_allowed; start rrserver to migrate the database first" >&2
        exit 1
    }
done

sqlite3 -bail "$db" <<SQL
PRAGMA busy_timeout=5000;
BEGIN IMMEDIATE;

INSERT OR IGNORE INTO privilege_sets(name, description, created_by)
VALUES ('demo-remote-superadmin', 'Demo remote full rig access', 'grant-remote-superadmin');

INSERT INTO privilege_set_entries(set_id, privilege, effect)
SELECT id, 'rig.full', 'allow' FROM privilege_sets WHERE name='demo-remote-superadmin'
ON CONFLICT(set_id, privilege) DO UPDATE SET effect='allow';

INSERT INTO privilege_set_assignments
    (set_id, subject_user, subject_host, resource_kind, resource_uuid,
     assigned_by, remote_allowed)
SELECT id, '$user', '$host', 'rig', '*', 'grant-remote-superadmin', 1
FROM privilege_sets WHERE name='demo-remote-superadmin'
ON CONFLICT(set_id, subject_user, subject_host, resource_kind, resource_uuid)
DO UPDATE SET remote_allowed=1, expires_at=NULL;

INSERT INTO resource_privileges
    (subject_user, subject_host, resource_kind, resource_uuid,
     privilege, effect, granted_by, remote_allowed)
VALUES
    ('$user','$host','room','*','room.join','allow','grant-remote-superadmin',1),
    ('$user','$host','site','*','remote-admin','allow','grant-remote-superadmin',1),
    ('$user','$host','site','*','admin','allow','grant-remote-superadmin',1),
    ('$user','$host','site','*','owner','allow','grant-remote-superadmin',1)
ON CONFLICT(subject_user,subject_host,resource_kind,resource_uuid,privilege)
DO UPDATE SET effect='allow', remote_allowed=1, expires_at=NULL;

COMMIT;
SQL

echo "Granted demo remote superadmin to ${user}@${host} on $db"
echo "WARNING: Broad demo-only permissions; revoke them before production."
