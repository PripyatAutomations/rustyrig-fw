#!/usr/bin/env bash
# Grant demo-only remote superadmin access on the DESTINATION rrserver.
# Usage: grant-remote-superadmin.sh [database] username@origin-site
# Examples:
#   sudo ./tools/grant-remote-superadmin.sh /var/lib/rustyrig/db/master.db 'admin@*'
#   sudo ./tools/grant-remote-superadmin.sh 'admin@rplywv00'
# WARNING: Development/demo use only; hardware/PTT safety remains enforced.
set -euo pipefail

usage() {
    echo "Usage: $0 [destination-master.db] <username@origin-site>" >&2
    echo "Default DB: path.db.master (or path.db) from /etc/rustyrig/rrserver.cfg" >&2
    exit 2
}
[[ $# -eq 1 || $# -eq 2 ]] || usage

if [[ $# -eq 2 ]]; then
    db=$1
    identity=$2
else
    identity=$1
    config=/etc/rustyrig/rrserver.cfg
    [[ -r $config ]] || { echo "Cannot read $config; specify the database explicitly" >&2; exit 1; }
    # Configuration is dotted-key INI, normally under [general].
    # Match exactly and do not execute/evaluate configuration text.
    db=$(awk '
        /^[[:space:]]*\[/ {
            in_general = ($0 ~ /^[[:space:]]*\[[[:space:]]*general[[:space:]]*\]/)
            next
        }
        in_general {
            line=$0
            sub(/^[[:space:]]*/, "", line)
            if (line ~ /^(path\.db\.master|path\.db)[[:space:]]*=/) {
                key=line; sub(/[[:space:]]*=.*/, "", key)
                value=line; sub(/^[^=]*=/, "", value)
                sub(/[[:space:]]*[;#].*$/, "", value)
                sub(/^[[:space:]]*/, "", value)
                sub(/[[:space:]]*$/, "", value)
                if (key == "path.db.master") master=value
                else legacy=value
            }
        }
        END { print master != "" ? master : legacy }
    ' "$config")
    [[ -n $db ]] || { echo "No path.db.master or path.db in $config; specify the database explicitly" >&2; exit 1; }
fi

# Keep wildcard host matching, e.g. admin@*. Quote the shell argument!
[[ $identity == *@* ]] || usage
user=${identity%@*}
host=${identity#*@}
[[ -n $user && -n $host ]] || usage

command -v sqlite3 >/dev/null 2>&1 || { echo "sqlite3 is required" >&2; exit 1; }
[[ -f $db ]] || { echo "Database does not exist: $db" >&2; exit 1; }
# Restrict values interpolated into SQL. This also disallows SQL metacharacters.
[[ ( $host == "*" || $host =~ ^[[:alnum:]_.-]+$ ) && $user =~ ^[[:alnum:]_.-]+$ ]] || {
    echo "Username may contain letters, digits, _, . and -; host may also be *" >&2
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
